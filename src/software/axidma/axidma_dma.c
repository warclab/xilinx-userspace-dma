/**
 * @file axidma_dma.c
 * @date Saturday, November 14, 2015 at 02:28:23 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This module contains the interface to the DMA engine for the AXI DMA module.
 *
 * @bug No known bugs.
 **/

// Kernel dependencies
#include <linux/delay.h>            // Milliseconds to jiffies converstion
#include <linux/wait.h>             // Completion related functions
#include <linux/dmaengine.h>        // DMA types and functions
#include <linux/slab.h>             // Allocation functions (ioremap)
#include <linux/amba/xilinx_dma.h>  // Xilinx DMA config structure
#include <linux/errno.h>            // Linux error codes

// Local dependencies
#include "axidma.h"                 // Internal definitions
#include "axidma_ioctl.h"           // IOCTL interface definition and types

/*----------------------------------------------------------------------------
 * Internal Definitions
 *----------------------------------------------------------------------------*/

// The default timeout for DMA is 10 seconds
#define AXIDMA_DMA_TIMEOUT      10000

// A convenient structure to pass between prep and start transfer functions
struct axidma_transfer {
    void *buf;                          // The buffer to use for transfer
    size_t buf_len;                     // The length of the transfer
    dma_addr_t dma_addr;                // The DMA address of the buffer
    dma_cookie_t cookie;                // The DMA cookie for the transfer
    enum dma_transfer_direction dir;    // The direction of the transfer
    bool wait;                          // Indicates if we should wait
    struct completion comp;             // A completion to use for waiting
};

/*----------------------------------------------------------------------------
 * DMA Operations Helper Functions
 *----------------------------------------------------------------------------*/

static struct dma_chan *axidma_get_chan(int device_id, int device_ids[],
                                        struct dma_chan *channels[], int len)
{
    int i;

    // Search for an array entry with a matching device id
    for (i = 0; i < len; i++)
    {
        if (device_ids[i] == device_id) {
            return channels[i];
        }
    }

    return NULL;
}

static void axidma_dma_completion(void *completion)
{
    complete(completion);
}

static int axidma_prep_transfer(struct dma_chan *chan,
                                struct axidma_transfer *dma_tfr)
{
    void *buf;
    size_t buf_len;
    struct dma_device *dma_dev;
    struct dma_async_tx_descriptor *dma_txnd;
    struct completion *dma_comp;
    struct xilinx_dma_config dma_config;
    enum dma_transfer_direction dma_dir;
    enum dma_ctrl_flags dma_flags;
    dma_cookie_t dma_cookie;
    dma_addr_t dma_addr;
    char *direction;
    int rc;

    // Get the fields from the structures
    buf = dma_tfr->buf;
    buf_len = dma_tfr->buf_len;
    dma_comp = &dma_tfr->comp;
    dma_dir = dma_tfr->dir;
    dma_dev = chan->device;
    direction = (dma_dir == DMA_MEM_TO_DEV) ? "transfer" : "receive";

    // Get the DMA address from the user virtual address
    dma_addr = axidma_uservirt_to_dma(buf);
    if (dma_addr == (dma_addr_t)NULL) {
        axidma_err("Unable to get DMA address for buffer at %p.\n", buf);
        rc = -EFAULT;
        goto ret;
    }

    // Configure the channel to only give one interrupt, with no delay
    dma_config.coalesc = 1;
    dma_config.delay = 0;
    rc = dma_dev->device_control(chan, DMA_SLAVE_CONFIG,
                                 (unsigned long)&dma_config);
    if (rc < 0) {
        axidma_err("Device control for the %s channel failed.\n", direction);
        goto stop_dma;
    }

    /* Configure the engine to send an interrupt acknowledgement upon
     * completion, and skip unmapping the buffer. */
    dma_flags = DMA_CTRL_ACK | DMA_COMPL_SKIP_DEST_UNMAP | DMA_PREP_INTERRUPT;
    dma_txnd = dmaengine_prep_slave_single(chan, dma_addr, buf_len, dma_dir,
                                           dma_flags);
    if (dma_txnd == NULL) {
        axidma_err("Unable to prepare the dma engine for the %s buffer.\n",
                   direction);
        rc = -EBUSY;
        goto stop_dma;
    }

    /* Initalize the completion for this channel, and setup the callback to
     * complete the completion. Submit the transaction to the DMA engine. */
    init_completion(dma_comp);
    dma_txnd->callback = axidma_dma_completion;
    dma_txnd->callback_param = dma_comp;
    dma_cookie = dma_txnd->tx_submit(dma_txnd);
    if (dma_submit_error(dma_cookie)) {
        axidma_err("Unable to submit the %s dma transaction to the engine.\n",
                   direction);
        rc = -EBUSY;
        goto stop_dma;
    }

    // Return the DMA cookie and address for the transaction
    dma_tfr->dma_addr = dma_addr;
    dma_tfr->cookie = dma_cookie;
    return 0;

stop_dma:
    dma_dev->device_control(chan, DMA_TERMINATE_ALL, 0);
ret:
    return rc;
}

static int axidma_start_transfer(struct dma_chan *chan,
                                 struct axidma_transfer *dma_tfr)
{
    struct completion *dma_comp;
    dma_cookie_t dma_cookie;
    enum dma_status status;
    char *direction;
    unsigned long timeout, time_remain;
    int rc;

    // Get the fields from the structures
    dma_comp = &dma_tfr->comp;
    dma_cookie = dma_tfr->cookie;
    direction = (dma_tfr->dir == DMA_MEM_TO_DEV) ? "transmit": "receive";

    // Flush all pending transaction in the dma engine for this channel
    dma_async_issue_pending(chan);

    // Wait for the completion timeout or the DMA to complete
    if (dma_tfr->wait) {
        timeout = msecs_to_jiffies(AXIDMA_DMA_TIMEOUT);
        time_remain = wait_for_completion_timeout(dma_comp, timeout);
        status = dma_async_is_tx_complete(chan, dma_cookie, NULL, NULL);

        if (time_remain == 0) {
            axidma_err("DMA %s transaction timed out.\n", direction);
            rc = -ETIME;
            goto stop_dma;
        } else if (status != DMA_SUCCESS) {
            axidma_err("DMA %s transaction did not succceed. Status is %d.\n",
                       direction, status);
            rc = -EBUSY;
            goto stop_dma;
        }
    }

    return 0;

stop_dma:
    chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
    return rc;
}

/*----------------------------------------------------------------------------
 * DMA Operations (Public Interface)
 *----------------------------------------------------------------------------*/

void axidma_get_num_channels(struct axidma_device *dev,
                             struct axidma_num_channels *num_chans)
{
    num_chans->num_tx_channels = dev->num_tx_channels;
    num_chans->num_rx_channels = dev->num_rx_channels;
    return;
}

void axidma_get_channel_ids(struct axidma_device *dev,
                            struct axidma_channel_ids *chan_ids)
{
    chan_ids->tx_device_ids = dev->tx_device_ids;
    chan_ids->rx_device_ids = dev->rx_device_ids;
    return;
}

int axidma_read_transfer(struct axidma_device *dev,
                         struct axidma_transaction *trans)
{
    int rc;
    struct dma_chan *rx_chan;

    // Setup receive transfer structure for DMA
    struct axidma_transfer rx_tfr = {
        .buf = trans->buf,
        .buf_len = trans->buf_len,
        .dir = DMA_DEV_TO_MEM,
        .wait = true,
    };

    // Get the channel with the given channel id
    rx_chan = axidma_get_chan(trans->device_id, dev->rx_device_ids,
                              dev->rx_chans, dev->num_rx_channels);
    if (rx_chan == NULL) {
        axidma_err("Invalid device id %d for receive channel.\n",
                   trans->device_id);
        return -ENODEV;
    }

    // Prepare the receive transfer
    rc = axidma_prep_transfer(rx_chan, &rx_tfr);
    if (rc < 0) {
        return rc;
    }

    // Submit the receive transfer, and wait for it to complete
    rc = axidma_start_transfer(rx_chan, &rx_tfr);
    if (rc < 0) {
        return rc;
    }

    return 0;

}

int axidma_write_transfer(struct axidma_device *dev,
                          struct axidma_transaction *trans)
{
    int rc;
    struct dma_chan *tx_chan;

    // Setup transmit transfer structure for DMA
    struct axidma_transfer tx_tfr = {
        .buf = trans->buf,
        .buf_len = trans->buf_len,
        .dir = DMA_MEM_TO_DEV,
        .wait = true,
    };

    // Get the channel with the given id
    tx_chan = axidma_get_chan(trans->device_id, dev->tx_device_ids,
                              dev->tx_chans, dev->num_tx_channels);
    if (tx_chan == NULL) {
        axidma_err("Invalid device id %d for transmit channel.\n",
                   trans->device_id);
        return -ENODEV;
    }

    // Prepare the transmit transfer
    rc = axidma_prep_transfer(tx_chan, &tx_tfr);
    if (rc < 0) {
        return rc;
    }

    // Submit the transmit transfer, and wait for it to complete
    rc = axidma_start_transfer(tx_chan, &tx_tfr);
    if (rc < 0) {
        return rc;
    }

    return 0;

}

/* Transfers data from the given source buffer out to the AXI DMA device, and
 * places the data received into the receive buffer. */
int axidma_rw_transfer(struct axidma_device *dev,
                       struct axidma_inout_transaction *trans)
{
    int rc;
    struct dma_chan *tx_chan;
    struct dma_chan *rx_chan;

    // Setup receive and trasmit transfer structures for DMA
    struct axidma_transfer tx_tfr = {
        .buf = trans->tx_buf,
        .buf_len = trans->tx_buf_len,
        .dir = DMA_MEM_TO_DEV,
        .wait = false,
    };
    struct axidma_transfer rx_tfr = {
        .buf = trans->rx_buf,
        .buf_len = trans->rx_buf_len,
        .dir = DMA_DEV_TO_MEM,
        .wait = true,
    };

    // Get the transmit and receive channels with the given ids.
    tx_chan = axidma_get_chan(trans->tx_device_id, dev->tx_device_ids,
                              dev->tx_chans, dev->num_tx_channels);
    if (tx_chan == NULL) {
        axidma_err("Invalid device id %d for transmit channel.\n",
                   trans->tx_device_id);
        return -ENODEV;
    }
    rx_chan = axidma_get_chan(trans->rx_device_id, dev->rx_device_ids,
                              dev->rx_chans, dev->num_rx_channels);
    if (rx_chan == NULL) {
        axidma_err("Invalid device id %d for receive channel.\n",
                   trans->rx_device_id);
        return -ENODEV;
    }

    // Prep both the receive and transmit transfers
    rc = axidma_prep_transfer(tx_chan, &tx_tfr);
    if (rc < 0) {
        return rc;
    }
    rc = axidma_prep_transfer(rx_chan, &rx_tfr);
    if (rc < 0) {
        return rc;
    }

    // Submit both transfers to the DMA engine, and wait on the receive transfer
    rc = axidma_start_transfer(tx_chan, &tx_tfr);
    if (rc < 0) {
        return rc;
    }
    rc = axidma_start_transfer(rx_chan, &rx_tfr);
    if (rc < 0) {
        return rc;
    }

    return 0;
}

/*----------------------------------------------------------------------------
 * Initialization and Cleanup
 *----------------------------------------------------------------------------*/

static bool axidma_dmadev_filter(struct dma_chan *chan, void *match)
{
    return *(int *)chan->private == (int)match;
}

static int axidma_probe_channels(struct axidma_device *dev,
                                 dma_cap_mask_t dma_mask)
{
    struct dma_chan *next_rx_chan, *next_tx_chan;
    int tx_chan_match, rx_chan_match;
    int device_id;

    // Probe the number of available receive and transmit channels
    dev->num_tx_channels = 0;
    dev->num_rx_channels = 0;
    for (device_id = 0; true; device_id++)
    {
        // Setup the matches for the transmit and receive channels
        tx_chan_match = PACK_DMA_MATCH(device_id, DMA_MEM_TO_DEV);
        rx_chan_match = PACK_DMA_MATCH(device_id, DMA_DEV_TO_MEM);

        // Request the matching channels
        next_tx_chan = dma_request_channel(dma_mask, axidma_dmadev_filter,
                                           (void *)tx_chan_match);
        next_rx_chan = dma_request_channel(dma_mask, axidma_dmadev_filter,
                                           (void *)rx_chan_match);

        // There are no more channels to be found
        // TODO: Could sparse id's be an issue here?
        if (next_tx_chan == NULL && next_rx_chan == NULL) {
            break;
        }

        /* We've found a receive/transmit channel. We don't have a place to
         * store it yet, so release the channel, we'll re-request it later. */
        if (next_tx_chan != NULL) {
            dev->num_tx_channels += 1;
            dma_release_channel(next_tx_chan);
        }
        if (next_rx_chan != NULL) {
            dev->num_rx_channels += 1;
            dma_release_channel(next_rx_chan);
        }
    }

    return 0;
}

int axidma_dma_init(struct axidma_device *dev)
{
    dma_cap_mask_t dma_mask;
    struct dma_chan *next_rx_chan, *next_tx_chan;
    int rx_chan_index, tx_chan_index;
    int tx_chan_match, rx_chan_match;
    int device_id;
    int rc;

    // Setup the desired DMA capabilities
    dma_cap_zero(dma_mask);
    dma_cap_set(DMA_SLAVE | DMA_PRIVATE, dma_mask);

    // Probe the AXI DMA device, and find the number of channels
    axidma_probe_channels(dev, dma_mask);

    // If no channels were found, initalize the arrays to NULL
    if (dev->num_tx_channels == 0 && dev->num_rx_channels == 0) {
        axidma_info("No tramsit or receive channels were found.\n");
        axidma_info("Found 0 receive channels and 0 transmit channels.\n");
        dev->tx_device_ids = NULL;
        dev->rx_device_ids = NULL;
        rc = 0;
        goto ret;
    }

    // Allocate arrays to store the channel pointers and device ids
    dev->tx_device_ids = kmalloc(dev->num_tx_channels*sizeof(int), GFP_KERNEL);
    if (dev->tx_device_ids == NULL) {
        axidma_err("Unable to allocate memory for transmit channels.\n");
        rc = -ENOMEM;
        goto ret;
    }
    dev->tx_chans = kmalloc(dev->num_tx_channels*sizeof(dev->tx_chans[0]),
                            GFP_KERNEL);
    if (dev->tx_chans == NULL) {
        axidma_err("Unable to allocate memory for transmit channels.\n");
        rc = -ENOMEM;
        goto free_tx_dev_ids;
    }
    dev->rx_device_ids = kmalloc(dev->num_rx_channels*sizeof(int), GFP_KERNEL);
    if (dev->rx_device_ids == NULL) {
        axidma_err("Unable to allocate memory for receive channels.\n");
        rc = -ENOMEM;
        goto free_tx_chans;
    }
    dev->rx_chans = kmalloc(dev->num_rx_channels*sizeof(dev->rx_chans[0]),
                            GFP_KERNEL);
    if (dev->rx_chans == NULL) {
        axidma_err("Unable to allocate memory for receive channels.\n");
        rc = -ENOMEM;
        goto free_rx_dev_ids;
    }

    // Request and acquire all the available channels
    rx_chan_index = 0;
    tx_chan_index = 0;
    for (device_id = 0; true; device_id++)
    {
        // Request the transmit and receive channels for the current id
        tx_chan_match = PACK_DMA_MATCH(device_id, DMA_MEM_TO_DEV);
        rx_chan_match = PACK_DMA_MATCH(device_id, DMA_DEV_TO_MEM);
        next_tx_chan = dma_request_channel(dma_mask, axidma_dmadev_filter,
                                           (void *)tx_chan_match);
        next_rx_chan = dma_request_channel(dma_mask, axidma_dmadev_filter,
                                           (void *)rx_chan_match);

        // There are no more channels to be found
        // TODO: Could sparse id's be an issue here?
        if (next_tx_chan == NULL && next_rx_chan == NULL) {
            break;
        }

        // Add any found transmit and receive channels to the array
        if (next_tx_chan != NULL) {
            dev->tx_device_ids[tx_chan_index] = device_id;
            dev->tx_chans[tx_chan_index] = next_tx_chan;
            tx_chan_index += 1;
        }
        if (next_rx_chan != NULL) {
            dev->rx_device_ids[rx_chan_index] = device_id;
            dev->rx_chans[rx_chan_index] = next_rx_chan;
            rx_chan_index += 1;
        }
    }

    // Inform the user of the DMA channels that were found
    axidma_info("Found %d receive channels and %d transmit channels.\n",
                dev->num_tx_channels, dev->num_rx_channels);
    return 0;

free_rx_dev_ids:
    kfree(dev->rx_device_ids);
free_tx_chans:
    kfree(dev->tx_chans);
free_tx_dev_ids:
    kfree(dev->tx_device_ids);
ret:
    return rc;
}

void axidma_dma_exit(struct axidma_device *dev)
{
    int i;
    struct dma_chan *chan;

    // Stop all running DMA transactions on all channels, and release
    for (i = 0; i < dev->num_tx_channels; i++)
    {
        chan = dev->tx_chans[i];
        chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
        dma_release_channel(chan);
    }
    for (i = 0; i < dev->num_rx_channels; i++)
    {
        chan = dev->rx_chans[i];
        chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
        dma_release_channel(chan);
    }

    // Free the channel arrays
    kfree(dev->tx_device_ids);
    kfree(dev->tx_chans);
    kfree(dev->rx_device_ids);
    kfree(dev->rx_chans);

    return;
}
