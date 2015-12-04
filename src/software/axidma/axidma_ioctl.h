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

#include <asm/ioctl.h>              // IOCTL macros

/*----------------------------------------------------------------------------
 * IOCTL Argument Definitions
 *----------------------------------------------------------------------------*/

// Forward declaration of the kernel's DMA channel type (opaque to userspace)
struct dma_chan;

// Direction from the persepctive of the processor
enum axidma_dir {
    AXIDMA_WRITE,                   // Transmits from memory to a device
    AXIDMA_READ                     // Transmits from a device to memory
};

enum axidma_type {
    AXIDMA_DMA,                     // Standard AXI DMA engine
    AXIDMA_VDMA                     // Specialized AXI video DMA enginge
};

// TODO: Channel really should not be here
struct axidma_chan {
    enum axidma_dir dir;            // The DMA direction of the channel
    enum axidma_type type;          // The DMA type of the channel
    int channel_id;                 // The identifier for the device
    struct dma_chan *chan;          // The DMA channel (ignore)
};

struct axidma_num_channels {
    int num_channels;               // Total DMA channels in the system
    int num_dma_tx_channels;        // DMA transmit channels available
    int num_dma_rx_channels;        // DMA receive channels available
    int num_vdma_tx_channels;       // VDMA transmit channels available
    int num_vdma_rx_channels;       // VDMA receive channels available
};

struct axidma_channel_info {
    struct axidma_chan *channels;   // Metadata about all available channels
};

struct axidma_transaction {
    bool wait;                      // Indicates if the call is blocking
    int channel_id;                 // The id of the DMA channel to use
    void *buf;                      // The buffer used for the transaction
    size_t buf_len;                 // The length of the buffer
};

struct axidma_inout_transaction {
    bool wait;                      // Indicates if the call is blocking
    int tx_channel_id;              // The id of the transmit DMA channel
    void *tx_buf;                   // The buffer containing the data to send
    size_t tx_buf_len;              // The length of the transmit buffer
    int rx_channel_id;              // The id of the receive DMA channel
    void *rx_buf;                   // The buffer to place the data in
    size_t rx_buf_len;              // The length of the receive buffer
};

struct axidma_video_transaction {
    int channel_id;             // The id of the DMA channel to transmit video
    void *buf1;                 // The first of the triple-buffers
    void *buf2;                 // The second of the triple-buffers
    void *buf3;                 // The third of the triple-buffers
    size_t width;               // The width of the image in pixels
    size_t height;              // The height of the image in lines
    size_t depth;               // The size of each pixel in bytes
};

/*----------------------------------------------------------------------------
 * IOCTL Interface
 *----------------------------------------------------------------------------*/

// The magic number used to distinguish IOCTL's for our device
#define AXIDMA_IOCTL_MAGIC              'W'

// Returns the number of DMA/VDMA channels available
#define AXIDMA_GET_NUM_DMA_CHANNELS     _IOW(AXIDMA_IOCTL_MAGIC, 0, \
                                             struct axidma_num_channels)
// Returns all available DMA/VDMA channel ids to the user
#define AXIDMA_GET_DMA_CHANNELS         _IOR(AXIDMA_IOCTL_MAGIC, 1, \
                                             struct axidma_channel_info)
// Receives data from the PL fabric
#define AXIDMA_DMA_READ                 _IOR(AXIDMA_IOCTL_MAGIC, 2, \
                                             struct axidma_transaction)
// Send data out over the PL fabric
#define AXIDMA_DMA_WRITE                _IOR(AXIDMA_IOCTL_MAGIC, 3, \
                                             struct axidma_transaction)
// Sends data out over the PL fabric, and then receives data back
#define AXIDMA_DMA_READWRITE            _IOR(AXIDMA_IOCTL_MAGIC, 4, \
                                             struct axidma_inout_transaction)
/* Repeatedly sends out the given double frame buffer over the PL fabirc, until
 * it is told to stop. Used to stream video out to a display device. */
#define AXIDMA_DMA_VIDEO_WRITE          _IOR(AXIDMA_IOCTL_MAGIC, 5, \
                                             struct axidma_video_transaction)
/* Stops the all transactions on the specified DMA channel. The channel must
 * be currently running an Video transaction. */
#define AXIDMA_STOP_DMA_CHANNEL         _IOR(AXIDMA_IOCTL_MAGIC, 6, \
                                             struct axidma_chan)

// The number of IOCTL's implemented, used for verification
#define AXIDMA_NUM_IOCTLS               7

#endif /* AXIDMA_IOCTL_H_ */
