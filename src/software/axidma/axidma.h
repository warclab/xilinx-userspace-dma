/**
 * @file axidma.h
 * @date Saturday, November 14, 2015 at 01:41:02 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This file contains the internal definitions and structures for AXI DMA module
 *
 * @bug No known bugs.
 **/

#ifndef AXIDMA_H_
#define AXIDMA_H_

// Kernel dependencies
#include <linux/kernel.h>           // Contains the definition for printk
#include <linux/device.h>           // Definitions for class and device structs
#include <linux/cdev.h>             // Definitions for character device structs

#include <linux/dmaengine.h>        // Definitions for DMA structures and types
#include <linux/amba/xilinx_dma.h>  // Xilinx DMA device ID's

// Local dependencies
#include "axidma_ioctl.h"           // IOCTL argument structures

/*----------------------------------------------------------------------------
 * Module Definitions
 *----------------------------------------------------------------------------*/

#define MODULE_NAME                 "axidma"

// Truncates the full __FILE__ path, only displaying the basename
#define __FILENAME__ \
    (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

// Convenient macros for printing out messages to the kernel log buffer
#define axidma_err(fmt, ...) \
    printk(KERN_ERR MODULE_NAME ": %s: %s: %d: " fmt, __FILENAME__, __func__, \
           __LINE__, ## __VA_ARGS__)
#define axidma_info(fmt, ...) \
    printk(KERN_ERR MODULE_NAME ": %s: %d: " fmt, __FILENAME__, __LINE__, \
           ## __VA_ARGS__)

// All of the meta-data needed for an axidma device
struct axidma_device {
    int num_devices;                // The number of devices
    unsigned int minor_num;         // The minor number of the device
    dev_t dev_num;                  // The device number of the device
    char *chrdev_name;              // The name of the character device
    struct device *device;          // Device structure for the character device
    struct class *dev_class;        // The device class for the character device
    struct cdev chrdev;             // The character device structure

    struct dma_chan *tx_chan;       // The transmit channel for DMA
    struct dma_chan *rx_chan;       // The receive channel for DMA
};

/*----------------------------------------------------------------------------
 * Character Device Definitions
 *----------------------------------------------------------------------------*/

// Default name of the character of the device
#define CHRDEV_NAME                 MODULE_NAME
// Default minor number for the device
#define MINOR_NUMBER                0
// The default number of character devices for DMA
#define NUM_DEVICES                 1

// Function prototypes
int axidma_chrdev_init(struct axidma_device *dev);
void axidma_chrdev_exit(struct axidma_device *dev);

/*----------------------------------------------------------------------------
 * Character Device Definitions
 *----------------------------------------------------------------------------*/

// The size of the memory for the board (this is configured for the Zedboard)
#define MEMORY_SIZE                 (512 * 1024 * 1024)

// The size of the region to use for contiguous DMA allocations
#define CMA_REGION_SIZE             (100 * 1024 * 1024)

// Packs the device id into a DMA match structure, to match DMA devices
#define PACK_DMA_MATCH(device_id, direction) \
    ((direction & 0xFF) | XILINX_DMA_IP_DMA |  \
     ((device_id) << XILINX_DMA_DEVICE_ID_SHIFT))


// Function prototypes
int axidma_dma_init(struct axidma_device *dev);
void axidma_dma_exit(struct axidma_device *dev);
int axidma_rw_transfer(struct axidma_device *dev,
                       struct axidma_transaction *trans);
dma_addr_t axidma_uservirt_to_dma(void *user_addr);

#endif /* AXIDMA_H_ */
