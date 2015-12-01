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

    int num_tx_channels;            // The number of transmit channels
    int num_rx_channels;            // The number of receive channels
    int *tx_device_ids;             // The device id's for the transmit channels
    int *rx_device_ids;             // The device id's for the receive channels
    struct dma_chan **tx_chans;     // The available transmit channels for DMA
    struct dma_chan **rx_chans;     // The available receive channels for DMA
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
 * DMA Device Definitions
 *----------------------------------------------------------------------------*/

// Packs the device id into a DMA match structure, to match DMA devices
#define PACK_DMA_MATCH(device_id, direction) \
    ((direction & 0xFF) | XILINX_DMA_IP_DMA |  \
     ((device_id) << XILINX_DMA_DEVICE_ID_SHIFT))

/*----------------------------------------------------------------------------
 * Function Prototypes
 *----------------------------------------------------------------------------*/

int axidma_dma_init(struct axidma_device *dev);
void axidma_dma_exit(struct axidma_device *dev);
void axidma_get_num_channels(struct axidma_device *dev,
                             struct axidma_num_channels *num_chans);
void axidma_get_channel_ids(struct axidma_device *dev,
                            struct axidma_channel_ids *chan_ids);
int axidma_read_transfer(struct axidma_device *dev,
                          struct axidma_transaction *trans);
int axidma_write_transfer(struct axidma_device *dev,
                          struct axidma_transaction *trans);
int axidma_rw_transfer(struct axidma_device *dev,
                       struct axidma_inout_transaction *trans);
int axidma_video_write_transfer(struct axidma_device *dev,
                                struct axidma_video_transaction *trans);
int axidma_stop_channel(struct axidma_device *dev, int device_id);
dma_addr_t axidma_uservirt_to_dma(void *user_addr);

#endif /* AXIDMA_H_ */
