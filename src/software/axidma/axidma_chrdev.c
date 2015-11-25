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
#include <linux/errno.h>            // Linux error codes

// Local dependencies
#include "axidma.h"             // Local definitions
#include "axidma_ioctl.h"       // IOCTL interface for the device

// TODO: Maybe this can be improved?
static struct axidma_device *axidma_dev;

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
    unsigned long alloc_size;
    void *addr;
    int rc;

    // Get the axidma device structure
    dev = file->private_data;

    // Determine the allocation size, and set the region as uncached
    alloc_size = vma->vm_end - vma->vm_start;
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    // Allocate the requested region
    // TODO:
    //axidma_malloc();
    addr = dev->dma_base_vaddr;

    // Map the region into userspace
    rc = remap_pfn_range(vma, vma->vm_start, __phys_to_pfn(virt_to_phys(addr)),
                         alloc_size, vma->vm_page_prot);
    if (rc < 0) {
        axidma_err("Unable to allocate address 0x%08lx, size %lu.\n",
                   vma->vm_start, alloc_size);
        return rc;
    }

    return 0;
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
