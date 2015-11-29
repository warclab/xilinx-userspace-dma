/**
 * @file axidma_ioctl.h
 * @date Tuesday, November 24, 2015 at 09:48:17 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This file contains the IOCTL interface definition. This is the interface from
 * userspace to the AXI DMA device to initiate DMA transactions and get
 * information about AXI DMA devices on the system.
 *
 * @bug No known bugs.
 **/

#ifndef AXIDMA_IOCTL_H_
#define AXIDMA_IOCTL_H_

#include <asm/ioctl.h>          // IOCTL macros

/*----------------------------------------------------------------------------
 * IOCTL Argument Definitions
 *----------------------------------------------------------------------------*/

struct axidma_num_channels {
    int num_rx_channels;    // The number of receive channels available
    int num_tx_channels;    // The number of transmit channels available
};

struct axidma_channel_ids {
    int requested_tx;       // Requested number of transmit channels
    int *tx_device_ids;     // Buffer to put the transmit channel device id's
    int requested_rx;       // Requested number of receive channels
    int *rx_device_ids;     // Buffer to put the receive channel device id's
};

struct axidma_transaction {
    int device_id;          // The id of the DMA device to use
    void *buf;              // The buffer used for the transaction
    size_t buf_len;         // The length of the buffer
};

struct axidma_inout_transaction {
    int tx_device_id;       // The id of the DMA device to use for transmit
    void *tx_buf;           // The buffer containing the data to send
    size_t tx_buf_len;      // The length of the transmit buffer
    int rx_device_id;       // The id of the DMA device to use for receive
    void *rx_buf;           // The buffer to place the data in
    size_t rx_buf_len;      // The length of the receive buffer
};

/*----------------------------------------------------------------------------
 * IOCTL Definitions
 *----------------------------------------------------------------------------*/

// The magic number used to distinguish IOCTL's for our device
#define AXIDMA_IOCTL_MAGIC              'W'

// Returns the number of receive and transmit channels available
#define AXIDMA_GET_NUM_DMA_CHANNELS     _IOW(AXIDMA_IOCTL_MAGIC, 0, \
                                             struct axidma_num_channels)
// Returns all available transmit and receive channels' device ids to the user
#define AXIDMA_GET_DMA_CHANNELS         _IOR(AXIDMA_IOCTL_MAGIC, 1, \
                                             struct axidma_channel_ids)
// Receives data from the PL fabric
#define AXIDMA_DMA_READ                 _IOR(AXIDMA_IOCTL_MAGIC, 2, \
                                             struct axidma_transaction)
// Send data out over the PL fabric
#define AXIDMA_DMA_WRITE                _IOR(AXIDMA_IOCTL_MAGIC, 3, \
                                             struct axidma_transaction)
// Sends data out over the PL fabric, and then receives data back
#define AXIDMA_DMA_READWRITE            _IOR(AXIDMA_IOCTL_MAGIC, 4, \
                                              struct axidma_inout_transaction)

// The number of IOCTL's implemented, used for verification
#define AXIDMA_NUM_IOCTLS       5

#endif /* AXIDMA_IOCTL_H_ */
