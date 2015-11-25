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

static void axidma_dma_completion(void *completion)
{
    complete(completion);
}

static int axidma_prep_transfer(struct dma_chan *chan, struct axidma_transfer *dma_tfr)
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

    // Map the buffer, and get the dma address
    dma_addr = dma_map_single(dma_dev->dev, buf, buf_len, dma_dir);
    if (dma_mapping_error(dma_dev->dev, dma_addr)) {
        axidma_err("Unable to map the %s buffer.\n", direction);
        // FIXME: Is this the correct return code?
        rc = -ENOMEM;
        goto ret;
    }

    // Configure the channel to only give one interrupt, with no delay
    dma_config.coalesc = 1;
    dma_config.delay = 0;
    rc = dma_dev->device_control(chan, DMA_SLAVE_CONFIG,
                                 (unsigned long)&dma_config);
    if (rc < 0) {
        axidma_err("Device control for the %s channel failed.\n", direction);
        goto unmap_dma;
    }

    /* Configure the engine to send an interrupt acknowledgement upon
     * completion, and skip unmapping the buffer. */
    dma_flags = DMA_CTRL_ACK | DMA_COMPL_SKIP_DEST_UNMAP | DMA_PREP_INTERRUPT;
    dma_txnd = dmaengine_prep_slave_single(chan, dma_addr, buf_len, dma_dir,
                                           dma_flags);
    if (dma_txnd == NULL) {
        axidma_err("Unable to prepare the dma engine for the %s buffer.\n",
                   direction);
        // FIXME: Is this the correct return code?
        rc = -EBUSY;
        goto unmap_dma;
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
        // FIXME: Is this the correct return code?
        rc = -EBUSY;
        goto unmap_dma;
    }

    // Return the DMA cookie and address for the transaction
    dma_tfr->dma_addr = dma_addr;
    dma_tfr->cookie = dma_cookie;
    return 0;

unmap_dma:
    dma_unmap_single(dma_dev->dev, dma_addr, buf_len, dma_dir);
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
            axidma_err("%s DMA transaction timed out.\n", direction);
            return -ETIME;
        } else if (status != DMA_SUCCESS) {
            axidma_err("%s DMA transaction did not succceed. Status is %d.\n",
                       direction, status);
            // FIXME: Is this the correct return code?
            return -ENOMEM;
        }
    }

    return 0;
}

/*----------------------------------------------------------------------------
 * DMA Operations (Public Interface)
 *----------------------------------------------------------------------------*/

int axidma_malloc(unsigned long size)
{
    // Get the next available element on the free list

    return 0;
}

/* Transfers data from the given source buffer out to the AXI DMA device, and
 * places the data received into the receive buffer. */
int axidma_rw_transfer(struct axidma_device *dev,
                       struct axidma_transaction *trans)
{
    int rc;

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

    // Prep both the receive and transmit transfers
    rc = axidma_prep_transfer(dev->tx_chan, &tx_tfr);
    if (rc < 0) {
        goto ret;
    }
    rc = axidma_prep_transfer(dev->rx_chan, &rx_tfr);
    if (rc < 0) {
        goto unmap_tx_buf;
    }

    // Submit both transfers to the DMA engine, and wait on the receive transfer
    rc = axidma_start_transfer(dev->tx_chan, &tx_tfr);
    if (rc < 0) {
        goto unmap_rx_buf;
    }
    rc = axidma_start_transfer(dev->rx_chan, &rx_tfr);
    if (rc < 0) {
        goto stop_rx_tfr;
    }

    return 0;

// FIXME: Do error handling
stop_rx_tfr:
unmap_rx_buf:
unmap_tx_buf:
ret:
    return rc;
}

/*----------------------------------------------------------------------------
 * Initialization and Cleanup
 *----------------------------------------------------------------------------*/

static bool axidma_dmadev_filter(struct dma_chan *chan, void *match)
{
    return *(int *)chan->private == (int)match;
}

int axidma_dma_init(struct axidma_device *dev)
{
    dma_cap_mask_t dma_mask;
    int tx_chan_match, rx_chan_match;
    int device_id;
    int rc;

    // Setup the desired DMA capabilities
    dma_cap_zero(dma_mask);
    dma_cap_set(DMA_SLAVE | DMA_PRIVATE, dma_mask);

    // TODO: Search for more than one AXI DMA device
    // Setup the request for the receive and transmit channels
    device_id = 0;
    tx_chan_match = PACK_DMA_MATCH(device_id, DMA_MEM_TO_DEV);
    rx_chan_match = PACK_DMA_MATCH(device_id, DMA_DEV_TO_MEM);

    // Request the transmit and receive channels
    dev->tx_chan = dma_request_channel(dma_mask, axidma_dmadev_filter,
                                       (void *)tx_chan_match);
    if (dev->tx_chan == NULL) {
        axidma_err("Could not find a transmit channel.\n");
        rc = -ENODEV;
        goto ret;
    }
    dev->rx_chan = dma_request_channel(dma_mask, axidma_dmadev_filter,
                                       (void *)rx_chan_match);
    if (dev->rx_chan == NULL) {
        axidma_err("Could not find a receive channel.\n");
        rc = -ENODEV;
        goto cleanup_tx;
    }

    // Inform the user of the DMA information
    axidma_info("Found %d receive channels and %d transmit channels.\n", 1, 1);
    return 0;

cleanup_tx:
    dma_release_channel(dev->tx_chan);
ret:
    return rc;
}

void axidma_dma_exit(struct axidma_device *dev)
{
    // TODO: Check if any dma transactions are running

    // Cleanup all DMA related structures
    dma_release_channel(dev->tx_chan);
    dma_release_channel(dev->rx_chan);
}
