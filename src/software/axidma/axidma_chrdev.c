/**
 * @file axidma_chrdev.c
 * @date Saturday, November 14, 2015 at 12:03:13 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This file contains the implementation of the character device for the AXI DMA
 * module.
 *
 * @bug No known bugs.
 **/

// Kernel dependencies
#include <linux/device.h>       // Device and class creation functions
#include <linux/cdev.h>         // Character device functions
#include <linux/ioctl.h>        // IOCTL macros and definitions
#include <linux/fs.h>           // File operations and file types
#include <linux/mm.h>           // Memory types and remapping functions
#include <asm/uaccess.h>        // Userspace memory access functions
#include <linux/slab.h>         // Kernel allocation functions
#include <linux/errno.h>        // Linux error codes

// Local dependencies
#include "axidma.h"             // Local definitions
#include "axidma_ioctl.h"       // IOCTL interface for the device

/*----------------------------------------------------------------------------
 * Internal Definitions
 *----------------------------------------------------------------------------*/

// TODO: Maybe this can be improved?
static struct axidma_device *axidma_dev;

// A struct to store information about each DMA region allocated
// TODO: Is a reference count required for this?
struct axidma_vma_data {
    struct axidma_device *dev;      // The AXI DMA device used
    void *dma_vaddr;                // The kernel virtual address of the region
    dma_addr_t dma_addr;            // The DMA address of the region
};

/*----------------------------------------------------------------------------
 * VMA Operations
 *----------------------------------------------------------------------------*/

void axidma_vma_close(struct vm_area_struct *vma)
{
    struct axidma_vma_data *vma_data;
    struct axidma_device *dev;
    void *dma_vaddr;
    dma_addr_t dma_addr;
    unsigned long alloc_size;

    // Get the AXI DMA device and the DMA information from the private data
    vma_data = vma->vm_private_data;
    dev = vma_data->dev;
    dma_vaddr = vma_data->dma_vaddr;
    dma_addr = vma_data->dma_addr;
    alloc_size = vma->vm_end - vma->vm_start;

    // Free the DMA region and the VMA data struct
    dma_free_coherent(dev->device, alloc_size, dma_vaddr, dma_addr);
    kfree(vma_data);

    return;
}

// The VMA operations for the AXI DMA device
static const struct vm_operations_struct axidma_vm_ops = {
    .close = axidma_vma_close,
};

/*----------------------------------------------------------------------------
 * File Operations
 *----------------------------------------------------------------------------*/

static int axidma_open(struct inode *inode, struct file *file)
{
    // Only the root user can open this device, and it must be exclusive
    if (!capable(CAP_SYS_ADMIN)) {
        axidma_err("Only root can open this device.");
        return -EACCES;
    } else if (!(file->f_flags & O_EXCL)) {
        axidma_err("O_EXCL must be specified for open()\n");
        return -EINVAL;
    }

    // Place the axidma structure in the private data of the file
    file->private_data = (void *)axidma_dev;
    return 0;
}

static int axidma_release(struct inode *inode, struct file *file)
{
    file->private_data = NULL;
    return 0;
}

static int axidma_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct axidma_device *dev;
    struct axidma_vma_data *vma_data;
    dma_addr_t dma_addr;
    void *dma_vaddr;
    unsigned long alloc_size, dma_pfn;
    int rc;

    // Get the axidma device structure
    dev = file->private_data;

    // Allocate a structure to store data about the DMA mapping
    vma_data = kmalloc(sizeof(*vma_data), GFP_KERNEL);
    if (vma_data == NULL) {
        axidma_err("Unable to allocate VMA data structure.");
        rc = -ENOMEM;
        goto ret;
    }

    // Allocate the requested region a contiguous and uncached for DMA
    alloc_size = vma->vm_end - vma->vm_start;
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    dma_vaddr = dma_alloc_coherent(NULL, alloc_size, &dma_addr,
                                   GFP_KERNEL);
    if (dma_vaddr == NULL) {
        axidma_err("Unable to allocate contiguous DMA memory region of size "
                   "%lu.\n", alloc_size);
        axidma_err("Please make sure that the kernel was built with CMA "
                   "support (CONFIG_CMA and related configs).\n");
        rc = -ENOMEM;
        goto free_vma_data;
    }

    /* Override the VMA close with our call, so that we can free the DMA region
     * when the memory region is closed. Pass in the data to do so. */
    vma_data->dma_vaddr = dma_vaddr;
    vma_data->dma_addr = dma_addr;
    vma_data->dev = dev;
    vma->vm_ops = &axidma_vm_ops;
    vma->vm_private_data = vma_data;

    // Map the region into userspace
    dma_pfn = __phys_to_pfn(virt_to_phys(dma_vaddr));
    rc = remap_pfn_range(vma, vma->vm_start, dma_pfn, alloc_size,
                         vma->vm_page_prot);
    if (rc < 0) {
        axidma_err("Unable to remap address %p to userspace address 0x%08lx, "
                   "size %lu.\n", dma_vaddr, vma->vm_start, alloc_size);
        goto free_dma_region;
    }

    return 0;

free_dma_region:
    dma_free_coherent(dev->device, alloc_size, dma_vaddr, dma_addr);
free_vma_data:
    kfree(vma_data);
ret:
    return rc;
}

static long axidma_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct axidma_device *dev;
    struct axidma_transaction trans_info;
    const void *__user arg_ptr;
    long rc;

    // Coerce the arguement as a userspace pointer
    arg_ptr = (const void __user *)arg;

    // Verify that this IOCTL is intended for our device, and is in range
    if (_IOC_TYPE(cmd) != AXIDMA_IOCTL_MAGIC) {
        axidma_err("IOCTL command magic number does not match.\n");
        return -ENOTTY;
    } else if (_IOC_NR(cmd) > AXIDMA_NUM_IOCTLS) {
        axidma_err("IOCTL command is out of range for this device.\n");
        return -ENOTTY;
    }

    /* Verify that the input argument can be accessed, and has the correct size
     * Note taht VERIFY_WRITE implies VERIFY_READ, so _IOC_RW is handled. */
    if ((_IOC_DIR(cmd) & _IOC_READ)) {
        if (!access_ok(VERIFY_WRITE, arg_ptr, _IOC_SIZE(cmd))) {
            axidma_err("Specified argument cannot be written to.\n");
            return -EFAULT;
        }
    } else if (_IOC_DIR(cmd) & _IOC_WRITE) {
        if (!access_ok(VERIFY_READ, arg_ptr, _IOC_SIZE(cmd))) {
            axidma_err("Specified argument cannot be read from.\n");
            return -EFAULT;
        }
    }

    // Get the axidma device from the file
    dev = file->private_data;

    // Perform the specified command
    switch (cmd) {
        case AXIDMA_DMA_READWRITE:
            if (copy_from_user(&trans_info, arg_ptr, sizeof(trans_info)) != 0) {
                axidma_err("Unable to copy transfer info from userspace for "
                           "AXIDMA_DMA_READWRITE\n");
                return -EFAULT;
            }
            rc = axidma_rw_transfer(dev, &trans_info);
            break;

        // Invalid command (already handled in preamble)
        default:
            return -ENOTTY;
    }

    return rc;
}

// The file operations for the AXI DMA device
static const struct file_operations axidma_fops = {
    .owner = THIS_MODULE,
    .open = axidma_open,
    .release = axidma_release,
    .mmap = axidma_mmap,
    .unlocked_ioctl = axidma_ioctl,
};

/*----------------------------------------------------------------------------
 * Initialization and Cleanup
 *----------------------------------------------------------------------------*/

int axidma_chrdev_init(struct axidma_device *dev)
{
    int rc;

    // Store a global pointer to the axidma device
    axidma_dev = dev;

    // Allocate a major and minor number region for the character device
    rc = alloc_chrdev_region(&dev->dev_num, dev->minor_num, dev->num_devices,
                             dev->chrdev_name);
    if (rc < 0) {
        axidma_err("Unable to allocate character device region.\n");
        goto ret;
    }

    // Create a device class for our device
    dev->dev_class = class_create(THIS_MODULE, dev->chrdev_name);
    if (IS_ERR(dev->dev_class)) {
        axidma_err("Unable to create a device class.\n");
        rc = PTR_ERR(dev->dev_class);
        goto free_chrdev_region;
    }

    /* Create a device for our module. This will create a file on the
     * filesystem, under "/dev/dev->chrdev_name". */
    dev->device = device_create(dev->dev_class, NULL, dev->dev_num, NULL,
                                dev->chrdev_name);
    if (IS_ERR(dev->device)) {
        axidma_err("Unable to create a device.\n");
        rc = PTR_ERR(dev->device);
        goto class_cleanup;
    }

    // Register our character device with the kernel
    cdev_init(&dev->chrdev, &axidma_fops);
    rc = cdev_add(&dev->chrdev, dev->dev_num, dev->num_devices);
    if (rc < 0) {
        axidma_err("Unable to add a character device.\n");
        goto device_cleanup;
    }

    return 0;

device_cleanup:
    device_destroy(dev->dev_class, dev->dev_num);
class_cleanup:
    class_destroy(dev->dev_class);
free_chrdev_region:
    unregister_chrdev_region(dev->dev_num, dev->num_devices);
ret:
    return rc;
}

void axidma_chrdev_exit(struct axidma_device *dev)
{
    // Cleanup all related character device structures
    cdev_del(&dev->chrdev);
    device_destroy(dev->dev_class, dev->dev_num);
    class_destroy(dev->dev_class);
    unregister_chrdev_region(dev->dev_num, dev->num_devices);

    return;
}
