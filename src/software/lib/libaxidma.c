/**
 * @file libaxidma.c
 * @date Saturday, December 05, 2015 at 10:10:39 AM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This is a simple library that wraps around the AXI DMA module,
 * allowing for the user to abstract away from the finer grained details.
 *
 * @bug No known bugs.
 **/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <sys/mman.h>           // Mmap system call
#include <sys/ioctl.h>          // IOCTL system call
#include <unistd.h>             // Close() system call
#include <errno.h>              // Error codes

#include "libaxidma.h"          // Local definitions
#include "axidma_ioctl.h"       // The IOCTL interface to AXI DMA

/*----------------------------------------------------------------------------
 * Internal definitions
 *----------------------------------------------------------------------------*/

// The structure that represents the AXI DMA device
struct axidma_dev {
    int fd;             // The file descriptor for the device
    int num_tx_chans;   // The total number of transmit channels
    int *tx_chan_ids;   // The channel id's for the transmit chans
    int num_rx_chans;   // The total number of receive channels
    int *rx_chan_ids;   // The channel id's for the receive channels
};

// Forward declarations for private helper functions
static int probe_channels(axidma_dev_t dev);
static int categorize_channels(axidma_dev_t dev,
        struct axidma_chan *channels, struct axidma_num_channels *num_chans);
static unsigned long dir_to_ioctl(enum axidma_dir dir);
static bool valid_channel(axidma_dev_t dev, int channel_id,
                          enum axidma_dir dir);

/*----------------------------------------------------------------------------
 * Public Interface
 *----------------------------------------------------------------------------*/

/* Initializes the AXI DMA device, returning a new handle to the
 * axidma_device. */
struct axidma_dev *axidma_init()
{
    axidma_dev_t axidma_dev;

    // Allocate a new AXI DMA device structure
    axidma_dev = malloc(sizeof(*axidma_dev));
    if (axidma_dev == NULL) {
        return NULL;
    }

    // Open the AXI DMA device
    axidma_dev->fd = open(AXIDMA_DEV_PATH, O_RDWR|O_EXCL);
    if (axidma_dev->fd < 0) {
        perror("Error opening AXI DMA device");
        fprintf(stderr, "Expected the AXI DMA device at the path `%s`\n",
                AXIDMA_DEV_PATH);
        free(axidma_dev);
        return NULL;
    }

    // Query the AXIDMA device for all of its channels
    if (probe_channels(axidma_dev) < 0) {
        close(axidma_dev->fd);
        free(axidma_dev);
        return NULL;
    }

    // Return the AXI DMA device to the user
    return axidma_dev;
}

// Tears down the given AXI DMA device structure
void axidma_destroy(axidma_dev_t dev)
{
    // Free the transmit and receive channel id arrays
    free(dev->tx_chan_ids);
    free(dev->rx_chan_ids);

    // Close the AXI DMA device
    if (close(dev->fd) < 0) {
        perror("Failed to close the AXI DMA device");
        assert(false);
    }

    // Free the device structure
    free(dev);
    return;
}

// Returns an array of all the available AXI DMA transmit channels
int *axidma_get_dma_tx(axidma_dev_t dev, int *num_channels)
{
    *num_channels = dev->num_tx_chans;
    return dev->tx_chan_ids;
}

// Returns an array of all the available AXI DMA receive channels
int *axidma_get_dma_rx(axidma_dev_t dev, int *num_channels)
{
    *num_channels = dev->num_rx_chans;
    return dev->rx_chan_ids;
}

/* Allocates a region of memory suitable for use with the AXI DMA driver. Note
 * that this is a quite expensive operation, and should be done at initalization
 * time. */
void *axidma_malloc(axidma_dev_t dev, size_t size)
{
    void *addr;

    // Call the device's mmap method to allocate the memory region
    addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, dev->fd, 0);
    if (addr == MAP_FAILED) {
        return NULL;
    }

    return addr;
}

/* This frees a region of memory that was allocated with a call to
 * axidma_malloc. The size passed in here must match the one used for that
 * call, or this function will throw an exception. */
void axidma_free(axidma_dev_t dev, void *addr, size_t size)
{
    // Silence the compiler
    (void)dev;

    if (munmap(addr, size) < 0) {
        perror("Failed to free the AXI DMA memory mapped region");
        assert(false);
    }

    return;
}

/* This performs a one-way transfer over AXI DMA, the direction being specified
 * by the user. The user determines if this is blocking or not with `wait. */
int axidma_oneway_transfer(axidma_dev_t dev, enum axidma_dir dir, int channel,
                           void *buf, size_t len, bool wait)
{
    int rc;
    struct axidma_transaction trans;
    unsigned long axidma_cmd;

    assert(dir == AXIDMA_READ || dir == AXIDMA_WRITE);
    assert(dir != AXIDMA_READ || valid_channel(dev, channel, AXIDMA_READ));
    assert(dir != AXIDMA_WRITE || valid_channel(dev, channel, AXIDMA_WRITE));

    // Setup the argument structure to the IOCTL
    trans.wait = wait;
    trans.channel_id = channel;
    trans.buf = buf;
    trans.buf_len = len;
    axidma_cmd = dir_to_ioctl(dir);

    // Perform the given transfer
    rc = ioctl(dev->fd, axidma_cmd, &trans);
    if (rc < 0) {
        perror("Failed to perform the AXI DMA transfer");
        return rc;
    }

    return 0;
}

/* This performs a two-way transfer over AXI DMA, both sending data out and
 * receiving it back over DMA. The user determines if this call is  blocking. */
int axidma_twoway_transfer(axidma_dev_t dev, int tx_channel, void *tx_buf,
        size_t tx_len, int rx_channel, void *rx_buf, size_t rx_len, bool wait)
{
    int rc;
    struct axidma_inout_transaction trans;

    assert(valid_channel(dev, tx_channel, AXIDMA_WRITE));
    assert(valid_channel(dev, rx_channel, AXIDMA_READ));

    // Setup the argument structure for the IOCTL
    trans.wait = wait;
    trans.tx_channel_id = tx_channel;
    trans.tx_buf = tx_buf;
    trans.tx_buf_len = tx_len;
    trans.rx_channel_id = rx_channel;
    trans.rx_buf = rx_buf;
    trans.rx_buf_len = rx_len;

    // Perform the read-write transfer
    rc = ioctl(dev->fd, AXIDMA_DMA_READWRITE, &trans);
    if (rc < 0) {
        perror("Failed to perform the AXI DMA read-write transfer");
    }

    return rc;
}

/* This function performs a video transfer over AXI DMA, setting up the DMA to
 * read from the given frame buffers on-demand continuously. This call is
 * always non-blocking. The transfer must be stopped with a call to
 * axidma_stop_transfer. */
int axidma_video_transfer(axidma_dev_t dev, int display_channel, size_t width,
        size_t height, size_t depth, void **frame_buffers, int num_buffers)
{
    int rc;
    struct axidma_video_transaction trans;

    assert(valid_channel(dev, display_channel, AXIDMA_WRITE));

    // Setup the argument structure for the IOCTL
    trans.channel_id = display_channel;
    trans.num_frame_buffers = num_buffers;
    trans.frame_buffers = frame_buffers;
    trans.width = width;
    trans.height = height;
    trans.depth = depth;

    // Perform the video transfer
    rc = ioctl(dev->fd, AXIDMA_DMA_VIDEO_WRITE, &trans);
    if (rc < 0) {
        perror("Failed to perform the AXI DMA video write transfer");
    }

    return rc;
}

/* This function stops all transfers on the given channel with the given
 * direction. This function is required to stop any video transfers, or any
 * non-blocking transfers. */
void axidma_stop_transfer(axidma_dev_t dev, int channel, enum axidma_dir dir)
{
    struct axidma_chan chan;

    assert(dir == AXIDMA_READ || dir == AXIDMA_WRITE);
    assert(dir != AXIDMA_READ || valid_channel(dev, channel, AXIDMA_READ));
    assert(dir != AXIDMA_WRITE || valid_channel(dev, channel, AXIDMA_WRITE));

    // Setup the argument structure for the IOCTL
    chan.channel_id = channel;
    chan.dir = dir;
    chan.type = AXIDMA_DMA;

    // Stop all transfers on the given DMA channel
    if (ioctl(dev->fd, AXIDMA_STOP_DMA_CHANNEL, &chan) < 0) {
        perror("Failed to stop the DMA channel");
        assert(false);
    }

    return;
}

/*----------------------------------------------------------------------------
 * Private Helper Functions
 *----------------------------------------------------------------------------*/

/* Probes the AXI DMA driver for all of the available channels. It places
 * returns an array of axidma_channel structures. */
static int probe_channels(axidma_dev_t dev)
{
    int rc;
    struct axidma_chan *channels;
    struct axidma_num_channels num_chan;
    struct axidma_channel_info channel_info;

    // Query the module for the total number of DMA channels
    rc = ioctl(dev->fd, AXIDMA_GET_NUM_DMA_CHANNELS, &num_chan);
    if (rc < 0) {
        perror("Unable to get the number of DMA channels");
        return rc;
    } else if (num_chan.num_channels == 0) {
        fprintf(stderr, "No DMA channels are present.\n");
        return -ENODEV;
    }

    // Allocate an array to hold the channel meta-data
    channels = malloc(num_chan.num_channels * sizeof(channels[0]));
    if (channels == NULL) {
        return -ENOMEM;
    }

    // Get the metdata about all the available channels
    channel_info.channels = channels;
    rc = ioctl(dev->fd, AXIDMA_GET_DMA_CHANNELS, &channel_info);
    if (rc < 0) {
        perror("Unable to get DMA channel information");
        free(channels);
        return rc;
    }

    // Extract the channel id's, and organize them by type
    rc = categorize_channels(dev, channels, &num_chan);
    free(channels);

    return rc;
}

/* Categorizes the DMA channels by their type and direction, getting their ID's
 * and placing them into separate arrays. */
static int categorize_channels(axidma_dev_t dev,
        struct axidma_chan *channels, struct axidma_num_channels *num_chan)
{
    int i;
    int tx_index, rx_index;
    struct axidma_chan *chan;

    // Allocate arrays for the DMA transmit and DMA receive channels
    dev->tx_chan_ids = malloc(num_chan->num_dma_tx_channels *
                              sizeof(dev->tx_chan_ids[0]));
    if (dev->tx_chan_ids == NULL) {
        return -ENOMEM;
    }
    dev->rx_chan_ids = malloc(num_chan->num_dma_rx_channels *
                              sizeof(dev->rx_chan_ids[0]));
    if (dev->rx_chan_ids == NULL) {
        free(dev->tx_chan_ids);
        return -ENOMEM;
    }

    // Place the DMA channel ID's into the appropiate array
    tx_index = 0;
    rx_index = 0;
    for (i = 0; i < num_chan->num_channels; i++)
    {
        chan = &channels[i];
        if (chan->dir == AXIDMA_WRITE && chan->type == AXIDMA_DMA) {
            dev->tx_chan_ids[tx_index] = chan->channel_id;
            tx_index += 1;
        } else if (chan->dir == AXIDMA_READ && chan->type == AXIDMA_DMA) {
            dev->rx_chan_ids[rx_index] = chan->channel_id;
            rx_index += 1;
        }
    }
    assert(tx_index == num_chan->num_dma_tx_channels);
    assert(rx_index == num_chan->num_dma_rx_channels);

    // Assign the length of the arrays
    dev->num_tx_chans = tx_index;
    dev->num_rx_chans = rx_index;

    return 0;
}

// Checks that the given channel is a valid id, searching through the arrays
static bool valid_channel(axidma_dev_t dev, int channel_id,
                          enum axidma_dir dir)
{
    int i, len;
    int *chan_ids;

    // Check that the enumeration is sound
    if (!(dir == AXIDMA_WRITE || dir == AXIDMA_READ)) {
        return false;
    }

    // Search through through the appropiate array based on the directino
    if (dir == AXIDMA_WRITE) {
        chan_ids = dev->tx_chan_ids;
        len = dev->num_tx_chans;
    } else {
        chan_ids = dev->rx_chan_ids;
        len =  dev->num_rx_chans;
    }

    // Search for the given ID
    for (i = 0; i < len; i++)
    {
        if (chan_ids[i] == channel_id) {
            return true;
        }
    }

    return false;
}

// Converts the AXI DMA direction to the corresponding ioctl for the transfer
static unsigned long dir_to_ioctl(enum axidma_dir dir)
{
    switch (dir)
    {
        case AXIDMA_READ:
            return AXIDMA_DMA_READ;
        case AXIDMA_WRITE:
            return AXIDMA_DMA_WRITE;
    }

    assert(false);
    return 0;
}
