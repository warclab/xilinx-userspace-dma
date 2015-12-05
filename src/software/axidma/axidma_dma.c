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

// The maximum permitted ID for a DMA channel
#define AXIDMA_MAX_ID           100

// A convenient structure to pass between prep and start transfer functions
struct axidma_transfer {
    int sg_len;                         // The length of the BD array
    struct scatterlist *sg_list;        // List of buffer descriptors
    bool wait;                          // Indicates if we should wait
    dma_cookie_t cookie;                // The DMA cookie for the transfer
    struct completion comp;             // A completion to use for waiting
    enum axidma_dir dir;                // The direction of the transfer
    enum axidma_type type;              // The type of the transfer (VDMA/DMA)

    // DMA and VDMA specific fields
    union {
        struct {
            bool cyclic_bd;             // Cyclic BD's, for continous trasfers
        } dma_tfr;
        struct {
            int width;                  // Width of the image in pixels
            int height;                 // Height of the image in lines
            int depth;                  // Size of each pixel in bytes
        } vdma_tfr;
    };
};

/*----------------------------------------------------------------------------
 * Enumeration Conversions
 *----------------------------------------------------------------------------*/

static enum dma_transfer_direction axidma_to_dma_dir(enum axidma_dir dma_dir)
{
    switch (dma_dir) {
        case AXIDMA_WRITE:
            return DMA_MEM_TO_DEV;
        case AXIDMA_READ:
            return DMA_DEV_TO_MEM;
    }

    BUG_ON("Invalid AXI DMA direction found.\n");
    return -1;
}

static char *axidma_dir_to_string(enum axidma_dir dma_dir)
{
    switch (dma_dir) {
        case AXIDMA_WRITE:
            return "transmit";
        case AXIDMA_READ:
            return "receive";
    }

    BUG_ON("Invalid AXI DMA direction found.\n");
    return NULL;
}

static int axidma_to_xilinx_type(enum axidma_type dma_type)
{
    switch (dma_type) {
        case AXIDMA_DMA:
            return XILINX_DMA_IP_DMA;
        case AXIDMA_VDMA:
            return XILINX_DMA_IP_VDMA;
    }

    BUG_ON("Invalid AXI DMA type found.\n");
    return -1;
}

static char *axidma_type_to_string(enum axidma_type dma_type)
{
    switch (dma_type) {
        case AXIDMA_DMA:
            return "DMA";
        case AXIDMA_VDMA:
            return "VDMA";
    }

    BUG_ON("Invalid AXI DMA type found.\n");
    return NULL;
}

/*----------------------------------------------------------------------------
 * DMA Operations Helper Functions
 *----------------------------------------------------------------------------*/

static int axidma_init_sg_entry(struct scatterlist *sg_list, int index,
                                void *buf, size_t buf_len)
{
    dma_addr_t dma_addr;

    // Get the DMA address from the user virtual address
    dma_addr = axidma_uservirt_to_dma(buf);
    if (dma_addr == (dma_addr_t)NULL) {
        axidma_err("Unable to get DMA address for buffer at %p.\n", buf);
        return -EFAULT;
    }

    // Initialize the scatter-gather table entry
    sg_dma_address(&sg_list[index]) = dma_addr;
    sg_dma_len(&sg_list[index]) = buf_len;

    return 0;
}

static struct axidma_chan *axidma_get_chan(struct axidma_device *dev,
    int chan_id, enum axidma_type chan_type, enum axidma_dir chan_dir)
{
    int i;
    struct axidma_chan *chan;

    // Search for an array entry with a matching device id
    for (i = 0; i < dev->num_chans; i++)
    {
        chan = &dev->channels[i];
        if (chan->type == chan_type && chan->dir == chan_dir &&
            chan->channel_id == chan_id) {
            return chan;
        }
    }

    return NULL;
}

static void axidma_setup_dma_config(struct xilinx_dma_config *dma_config,
        enum dma_transfer_direction direction, bool cyclic_bd)
{
    dma_config->direction = direction;  // Either to memory or from memory
    dma_config->coalesc = 1;            // Interrupt for one transfer completion
    dma_config->delay = 0;              // Disable the delay counter interrupt
    dma_config->reset = 0;              // Don't reset the DMA engine
    dma_config->cyclic_bd = cyclic_bd;  // Select a continuous transfer or not
    return;
}

static void axidma_setup_vdma_config(struct xilinx_vdma_config *dma_config,
                                     int width, int height, int depth)
{
    dma_config->vsize = height;         // Height of the image (in lines)
    dma_config->hsize = width * depth;  // Width of the image (in bytes)
    dma_config->stride = width * depth; // Number of bytes to process per line
    dma_config->frm_dly = 0;            // Number of frames to delay
    dma_config->gen_lock = 0;           // No genlock, VDMA runs freely
    dma_config->master = 0;             // VDMA is the genlock master
    dma_config->frm_cnt_en = 0;         // No interrupts based on frame count
    dma_config->park = 0;               // Continuously process all frames
    dma_config->park_frm = 0;           // Frame to stop (park) at (N/A)
    dma_config->coalesc = 0;            // No transfer completion interrupts
    dma_config->delay = 0;              // Disable the delay counter interrupt
    dma_config->reset = 0;              // Don't reset the channel
    dma_config->ext_fsync = 0;          // VDMA handles synchronizes itself
    return;
}

static void axidma_dma_completion(void *completion)
{
    if (completion != NULL) {
        complete(completion);
    }
}

static int axidma_prep_transfer(struct axidma_chan *axidma_chan,
                                struct axidma_transfer *dma_tfr)
{
    struct dma_chan *chan;
    struct dma_device *dma_dev;
    struct dma_async_tx_descriptor *dma_txnd;
    struct completion *dma_comp;
    struct xilinx_vdma_config vdma_config;
    struct xilinx_dma_config dma_config;
    void *config;
    enum dma_transfer_direction dma_dir;
    enum dma_ctrl_flags dma_flags;
    struct scatterlist *sg_list;
    int sg_len;
    dma_cookie_t dma_cookie;
    char *direction, *type;
    int rc;

    // Get the fields from the structures
    chan = axidma_chan->chan;
    dma_comp = &dma_tfr->comp;
    dma_dir = axidma_to_dma_dir(dma_tfr->dir);
    dma_dev = chan->device;
    sg_list = dma_tfr->sg_list;
    sg_len = dma_tfr->sg_len;
    direction = axidma_dir_to_string(dma_tfr->dir);
    type = axidma_type_to_string(dma_tfr->type);

    // Configure the channel appropiately based on whether it's DMA or VDMA
    if (dma_tfr->type == AXIDMA_DMA) {
        axidma_setup_dma_config(&dma_config, dma_dir,
                                dma_tfr->dma_tfr.cyclic_bd);
        config = &dma_config;
    } else if (dma_tfr->type == AXIDMA_VDMA) {
        axidma_setup_vdma_config(&vdma_config, dma_tfr->vdma_tfr.width,
            dma_tfr->vdma_tfr.height, dma_tfr->vdma_tfr.depth);
        config = &vdma_config;
    }
    rc = dma_dev->device_control(chan, DMA_SLAVE_CONFIG, (unsigned long)config);
    if (rc < 0) {
        axidma_err("Device control for the %s %s channel failed.\n", type,
                   direction);
        goto stop_dma;
    }

    /* Configure the engine to send an interrupt acknowledgement upon
     * completion, and skip unmapping the buffer. */
    dma_flags = DMA_CTRL_ACK | DMA_COMPL_SKIP_DEST_UNMAP | DMA_PREP_INTERRUPT;
    dma_txnd = dma_dev->device_prep_slave_sg(chan, sg_list, sg_len, dma_dir,
                                             dma_flags, NULL);
    if (dma_txnd == NULL) {
        axidma_err("Unable to prepare the dma engine for the %s %s buffer.\n",
                   type, direction);
        rc = -EBUSY;
        goto stop_dma;
    }

    /* If we're going to wait for this channel, initialize the completion for
     * the channel, and setup the callback to complete it. */
    if (dma_tfr->wait) {
        init_completion(dma_comp);
        dma_txnd->callback_param = dma_comp;
        dma_txnd->callback = axidma_dma_completion;
    } else {
        dma_txnd->callback_param = NULL;
        dma_txnd->callback = NULL;
    }
    dma_cookie = dma_txnd->tx_submit(dma_txnd);
    if (dma_submit_error(dma_cookie)) {
        axidma_err("Unable to submit the %s %s transaction to the engine.\n",
                   direction, type);
        rc = -EBUSY;
        goto stop_dma;
    }

    // Return the DMA cookie for the transaction
    dma_tfr->cookie = dma_cookie;
    return 0;

stop_dma:
    dma_dev->device_control(chan, DMA_TERMINATE_ALL, 0);
    return rc;
}

static int axidma_start_transfer(struct axidma_chan *chan,
                                 struct axidma_transfer *dma_tfr)
{
    struct completion *dma_comp;
    dma_cookie_t dma_cookie;
    enum dma_status status;
    char *direction, *type;
    unsigned long timeout, time_remain;
    int rc;

    // Get the fields from the structures
    dma_comp = &dma_tfr->comp;
    dma_cookie = dma_tfr->cookie;
    direction = axidma_dir_to_string(dma_tfr->dir);
    type = axidma_type_to_string(dma_tfr->type);

    // Flush all pending transaction in the dma engine for this channel
    dma_async_issue_pending(chan->chan);

    // Wait for the completion timeout or the DMA to complete
    if (dma_tfr->wait) {
        timeout = msecs_to_jiffies(AXIDMA_DMA_TIMEOUT);
        time_remain = wait_for_completion_timeout(dma_comp, timeout);
        status = dma_async_is_tx_complete(chan->chan, dma_cookie, NULL, NULL);

        if (time_remain == 0) {
            axidma_err("%s %s transaction timed out.\n", type, direction);
            rc = -ETIME;
            goto stop_dma;
        } else if (status != DMA_SUCCESS) {
            axidma_err("%s %s transaction did not succceed. Status is %d.\n",
                       type, direction, status);
            rc = -EBUSY;
            goto stop_dma;
        }
    }

    return 0;

stop_dma:
    chan->chan->device->device_control(chan->chan, DMA_TERMINATE_ALL, 0);
    return rc;
}

/*----------------------------------------------------------------------------
 * DMA Operations (Public Interface)
 *----------------------------------------------------------------------------*/

void axidma_get_num_channels(struct axidma_device *dev,
                             struct axidma_num_channels *num_chans)
{
    num_chans->num_channels = dev->num_chans;
    num_chans->num_dma_tx_channels = dev->num_dma_tx_chans;
    num_chans->num_dma_rx_channels = dev->num_dma_rx_chans;
    num_chans->num_vdma_tx_channels = dev->num_vdma_tx_chans;
    num_chans->num_vdma_rx_channels = dev->num_vdma_rx_chans;
    return;
}

void axidma_get_channel_info(struct axidma_device *dev,
                             struct axidma_channel_info *chan_info)
{
    chan_info->channels = dev->channels;
    return;
}

int axidma_read_transfer(struct axidma_device *dev,
                         struct axidma_transaction *trans)
{
    int rc;
    struct axidma_chan *rx_chan;
    struct scatterlist sg_list;

    // Setup receive transfer structure for DMA
    struct axidma_transfer rx_tfr = {
        .sg_list = &sg_list,
        .sg_len = 1,
        .dir = AXIDMA_READ,
        .type = AXIDMA_DMA,
        .wait = trans->wait,
        .dma_tfr.cyclic_bd = false,
    };

    // Setup the scatter-gather list for the transfer (only one entry)
    sg_init_table(rx_tfr.sg_list, rx_tfr.sg_len);
    rc = axidma_init_sg_entry(rx_tfr.sg_list, 0, trans->buf, trans->buf_len);
    if (rc < 0) {
        return rc;
    }

    // Get the channel with the given channel id
    rx_chan = axidma_get_chan(dev, trans->channel_id, AXIDMA_DMA, AXIDMA_READ);
    if (rx_chan == NULL) {
        axidma_err("Invalid device id %d for DMA receive channel.\n",
                   trans->channel_id);
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
    struct axidma_chan *tx_chan;
    struct scatterlist sg_list;

    // Setup transmit transfer structure for DMA
    struct axidma_transfer tx_tfr = {
        .sg_list = &sg_list,
        .sg_len = 1,
        .dir = AXIDMA_WRITE,
        .type = AXIDMA_DMA,
        .wait = trans->wait,
        .dma_tfr.cyclic_bd = false,
    };

    // Setup the scatter-gather list for the transfer (only one entry)
    sg_init_table(tx_tfr.sg_list, tx_tfr.sg_len);
    rc = axidma_init_sg_entry(tx_tfr.sg_list, 0, trans->buf, trans->buf_len);
    if (rc < 0) {
        return rc;
    }

    // Get the channel with the given id
    tx_chan = axidma_get_chan(dev, trans->channel_id, AXIDMA_DMA, AXIDMA_WRITE);
    if (tx_chan == NULL) {
        axidma_err("Invalid device id %d for DMA transmit channel.\n",
                   trans->channel_id);
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
    struct axidma_chan *tx_chan, *rx_chan;
    struct scatterlist tx_sg_list, rx_sg_list;

    // Setup receive and trasmit transfer structures for DMA
    struct axidma_transfer tx_tfr = {
        .sg_list = &tx_sg_list,
        .sg_len = 1,
        .dir = AXIDMA_WRITE,
        .type = AXIDMA_DMA,
        .wait = false,
        .dma_tfr.cyclic_bd = false,
    };
    struct axidma_transfer rx_tfr = {
        .sg_list = &rx_sg_list,
        .sg_len = 1,
        .dir = AXIDMA_READ,
        .type = AXIDMA_DMA,
        .wait = trans->wait,
        .dma_tfr.cyclic_bd = false,
    };

    // Setup the scatter-gather list for the transfers (only one entry)
    sg_init_table(tx_tfr.sg_list, tx_tfr.sg_len);
    rc = axidma_init_sg_entry(tx_tfr.sg_list, 0, trans->tx_buf,
                              trans->tx_buf_len);
    if (rc < 0) {
        return rc;
    }
    sg_init_table(rx_tfr.sg_list, rx_tfr.sg_len);
    rc = axidma_init_sg_entry(rx_tfr.sg_list, 0, trans->rx_buf,
                              trans->rx_buf_len);
    if (rc < 0) {
        return rc;
    }

    // Get the transmit and receive channels with the given ids.
    tx_chan = axidma_get_chan(dev, trans->tx_channel_id, AXIDMA_DMA,
                              AXIDMA_WRITE);
    if (tx_chan == NULL) {
        axidma_err("Invalid device id %d for DMA transmit channel.\n",
                   trans->tx_channel_id);
        return -ENODEV;
    }
    rx_chan = axidma_get_chan(dev, trans->rx_channel_id, AXIDMA_DMA,
                              AXIDMA_READ);
    if (rx_chan == NULL) {
        axidma_err("Invalid device id %d for DMA receive channel.\n",
                   trans->rx_channel_id);
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

int axidma_video_write_transfer(struct axidma_device *dev,
                                struct axidma_video_transaction *trans)
{
    int rc, i;
    size_t image_size;
    struct axidma_chan *tx_chan;
    struct scatterlist *sg_list;

    // Setup transmit transfer structure for DMA
    struct axidma_transfer tx_tfr = {
        .sg_len = trans->num_frame_buffers,
        .dir = AXIDMA_WRITE,
        .type = AXIDMA_DMA,
        .wait = false,
        .dma_tfr.cyclic_bd = true,
    };

    // Allocate an array to store the scatter list structures for the buffers
    tx_tfr.sg_list = kmalloc(tx_tfr.sg_len * sizeof(*sg_list), GFP_KERNEL);
    if (tx_tfr.sg_list == NULL) {
        axidma_err("Unable to allocate memory for the scatter-gather list.\n");
        rc = -ENOMEM;
        goto ret;
    }

    // For each frame, setup a scatter-gather entry
    image_size = trans->width * trans->height * trans->depth;
    for (i = 0; i < tx_tfr.sg_len; i++)
    {
        rc = axidma_init_sg_entry(tx_tfr.sg_list, i, trans->frame_buffers[i],
                                  image_size);
        if (rc < 0) {
            goto free_sg_list;
        }
    }

    // Get the channel with the given id
    tx_chan = axidma_get_chan(dev, trans->channel_id, AXIDMA_DMA,
                              AXIDMA_WRITE);
    if (tx_chan == NULL) {
        axidma_err("Invalid device id %d for DMA transmit channel.\n",
                   trans->channel_id);
        rc = -ENODEV;
        goto free_sg_list;
    }

    // Prepare the transmit transfer
    rc = axidma_prep_transfer(tx_chan, &tx_tfr);
    if (rc < 0) {
        goto free_sg_list;
    }

    // Submit the transfer, and immediately return
    rc = axidma_start_transfer(tx_chan, &tx_tfr);

free_sg_list:
    kfree(tx_tfr.sg_list);
ret:
    return 0;
}

int axidma_stop_channel(struct axidma_device *dev,
                        struct axidma_chan *chan_info)
{
    struct axidma_chan *chan;

    // Get the transmit and receive channels with the given ids.
    chan = axidma_get_chan(dev, chan_info->channel_id, chan_info->type,
                           chan_info->dir);
    if (chan == NULL) {
        axidma_err("Invalid channel id %d for %s %s channel.\n",
            chan_info->channel_id, axidma_type_to_string(chan_info->type),
            axidma_dir_to_string(chan_info->dir));
        return -ENODEV;
    }

    // Terminate all DMA transactions on the given channel
    return chan->chan->device->device_control(chan->chan, DMA_TERMINATE_ALL, 0);
}

/*----------------------------------------------------------------------------
 * Initialization and Cleanup
 *----------------------------------------------------------------------------*/

static bool axidma_dmadev_filter(struct dma_chan *chan, void *match)
{
    return *(int *)chan->private == (int)match;
}

static void axidma_probe_chan(struct axidma_device *dev, int channel_id,
    enum axidma_dir channel_dir, enum axidma_type channel_type,
    dma_cap_mask_t dma_mask, int *num_type_chans)
{
    struct dma_chan *chan;
    int chan_type, match;
    enum dma_transfer_direction chan_dir;

    // Pack together a match structure to identify the channel
    chan_type = axidma_to_xilinx_type(channel_type);
    chan_dir = axidma_to_dma_dir(channel_dir);
    match = PACK_DMA_MATCH(channel_id, chan_type, chan_dir);
    chan = dma_request_channel(dma_mask, axidma_dmadev_filter, (void *)match);

    /* If we have a place to store the channels, do so. Otherwsie, this is the
     * first probe, so we're counting channels, so we release them. */
    if (chan != NULL && dev->channels == NULL) {
        dma_release_channel(chan);
        dev->num_chans += 1;
        *num_type_chans += 1;
    } else if (chan != NULL) {
        dev->channels[dev->num_chans].dir = channel_dir;
        dev->channels[dev->num_chans].type = channel_type;
        dev->channels[dev->num_chans].channel_id = channel_id;
        dev->channels[dev->num_chans].chan = chan;
        dev->num_chans += 1;
        *num_type_chans += 1;
    }

    return;
}

static void axidma_probe_channels(struct axidma_device *dev,
                                  dma_cap_mask_t dma_mask)
{
    int channel_id;

    // Probe the available DMA receive and transmit channels
    dev->num_chans = 0;
    dev->num_dma_tx_chans = 0;
    dev->num_dma_rx_chans = 0;
    for (channel_id = 0; channel_id < AXIDMA_MAX_ID; channel_id++)
    {
        axidma_probe_chan(dev, channel_id, AXIDMA_WRITE, AXIDMA_DMA, dma_mask,
                          &dev->num_dma_tx_chans);
        axidma_probe_chan(dev, channel_id, AXIDMA_READ, AXIDMA_DMA, dma_mask,
                          &dev->num_dma_rx_chans);
    }

    // Probe the available VDMA receive and transmit channels
    dev->num_vdma_tx_chans = 0;
    dev->num_vdma_rx_chans = 0;
    for (channel_id = 0; channel_id < AXIDMA_MAX_ID; channel_id++)
    {
        axidma_probe_chan(dev, channel_id, AXIDMA_WRITE, AXIDMA_VDMA, dma_mask,
                          &dev->num_vdma_tx_chans);
        axidma_probe_chan(dev, channel_id, AXIDMA_READ, AXIDMA_VDMA, dma_mask,
                          &dev->num_vdma_rx_chans);
    }

    return;
}

int axidma_dma_init(struct axidma_device *dev)
{
    dma_cap_mask_t dma_mask;

    // Setup the desired DMA capabilities
    dma_cap_zero(dma_mask);
    dma_cap_set(DMA_SLAVE | DMA_PRIVATE, dma_mask);

    // Probe the AXI DMA devices, and find the number of channels
    dev->channels = NULL;
    axidma_probe_channels(dev, dma_mask);

    if (dev->num_chans == 0) {
        axidma_info("No DMA channels were found.\n");
        axidma_info("DMA: Found 0 transmit channels and 0 receive channels.\n");
        axidma_info("VDMA: Found 0 transmit channels and 0 receive channels."
                    "\n");
        return 0;
    }

    // Allocate an array to store all channel metadata structures
    dev->channels= kmalloc(dev->num_chans*sizeof(dev->channels[0]), GFP_KERNEL);
    if (dev->channels == NULL) {
        axidma_err("Unable to allocate memory for channel structures.\n");
        return -ENOMEM;
    }

    // Probe the AXI DMA devices, but store the channel structs this time
    axidma_probe_channels(dev, dma_mask);

    // Inform the user of the DMA channels that were found
    axidma_info("DMA: Found %d transmit channels and %d receive channels.\n",
                dev->num_dma_tx_chans, dev->num_dma_rx_chans);
    axidma_info("VDMA: Found %d transmit channels and %d receive channels.\n",
                dev->num_vdma_tx_chans, dev->num_vdma_rx_chans);
    return 0;
}

void axidma_dma_exit(struct axidma_device *dev)
{
    int i;
    struct dma_chan *chan;

    // Stop all running DMA transactions on all channels, and release
    for (i = 0; i < dev->num_chans; i++)
    {
        chan = dev->channels[i].chan;
        chan->device->device_control(chan, DMA_TERMINATE_ALL, 0);
        dma_release_channel(chan);
    }

    // Free the channel array
    kfree(dev->channels);

    return;
}
