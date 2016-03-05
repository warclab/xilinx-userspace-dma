/**
 * @file axi_dma.c
 * @date Saturday, November 14, 2015 at 11:20:00 AM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This file contains the top level functions for AXI DMA module.
 *
 * @bug No known bugs.
 **/

// Kernel dependencies
#include <linux/module.h>       // Module init and exit macros
#include <linux/moduleparam.h>  // Module param macro
#include <linux/stat.h>         // Module parameter permission values

// Local dependencies
#include "axidma.h"             // Macros and definitions

static struct axidma_device axidma_dev;

/*----------------------------------------------------------------------------
 * Module Parameters
 *----------------------------------------------------------------------------*/

// The name to use for the character device. This is "axidma" by default.
static char *chrdev_name = CHRDEV_NAME;
module_param(chrdev_name, charp, S_IRUGO);

// The minor number to use for the character device. 0 by default.
static int minor_num = MINOR_NUMBER;
module_param(minor_num, int, S_IRUGO);

/*----------------------------------------------------------------------------
 * Module Initialization and Exit
 *----------------------------------------------------------------------------*/

static int __init axidma_init(void)
{
    int rc;

    // Initialize the DMA interface
    rc = axidma_dma_init(&axidma_dev);
    if (rc < 0) {
        return rc;
    }

    // Assign the character device name, minor number, and number of devices
    axidma_dev.chrdev_name = chrdev_name;
    axidma_dev.minor_num = minor_num;
    axidma_dev.num_devices = NUM_DEVICES;

    // Initialize the character device for the module.
    rc = axidma_chrdev_init(&axidma_dev);
    if (rc < 0) {
        axidma_dma_exit(&axidma_dev);
        return rc;
    }

    return 0;
}

static void __exit axidma_exit(void)
{
    // Cleanup the character device structures
    axidma_chrdev_exit(&axidma_dev);

    // Cleanup the DMA structures
    axidma_dma_exit(&axidma_dev);

    return;
}

module_init(axidma_init);
module_exit(axidma_exit);

MODULE_AUTHOR("Brandon Perez");
MODULE_AUTHOR("Jared Choi");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Module to provide a userspace interface for transferring "
                   "data from the processor to the logic fabric via AXI DMA.");
