/**
 * @file version_portability.h
 * @date Saturday, April 09, 2016 at 10:08:33 AM EDT
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This header file deals with portability for the module across the 3.x and 4.x
 * versions of Linux. This file checks the version and updates the definitions
 * appropiately.
 *
 * @bug No known bugs.
 **/

#ifndef VERSION_PORTABILITY_H_
#define VERSION_PORTABILITY_H_

#include <linux/version.h>          // Linux version macros
#include <linux/dmaengine.h>        // Definitions for DMA structures and types

/* Between 3.x and 4.x, the path to Xilinx's DMA include file changes. However,
 * in some 4.x kernels, the path is still the old one from 3.x. The macro is
 * defined by the Makefile, when specified by the user. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0) && \
    !defined(XILINX_DMA_INCLUDE_PATH_FIXUP)
#include <linux/dma/xilinx_dma.h>   // Xilinx DMA config structures
#else
#include <linux/amba/xilinx_dma.h>  // Xilinx DMA config structures
#endif

/*----------------------------------------------------------------------------
 * Common Functions
 *----------------------------------------------------------------------------*/

// Convert the AXI DMA direction enumeration to a DMA direction enumeration
static inline
enum dma_transfer_direction axidma_to_dma_dir(enum axidma_dir dma_dir)
{
    BUG_ON(dma_dir != AXIDMA_WRITE && dma_dir != AXIDMA_READ);
    return (dma_dir == AXIDMA_WRITE) ? DMA_MEM_TO_DEV : DMA_DEV_TO_MEM;
}

/*----------------------------------------------------------------------------
 * Linux 4.x Compatbility
 *----------------------------------------------------------------------------*/

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)

#warning("This driver only supports Linux 3.x and 4.x versions. Linux 5.x " \
         "and greater versions are untested")

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)

// DMA_SUCCESS was renamed to DMA_COMPLETE (indicates a DMA transaction is done)
#define DMA_SUCCESS                 DMA_COMPLETE

// The skip destination unmap DMA control option was removed in 4.x
#define DMA_COMPL_SKIP_DEST_UNMAP   0

/* The xilinx_dma_config structure was removed in 4.x, so we create a dummy one
 * here. AXI DMA no longer implements slave config, so this is ignored. */
struct xilinx_dma_config {
    int dummy;
};

/* Setup the config structure for DMA. In 4.x, the DMA config structure was
 * removed, so we can safely just set it to zero here. */
static inline
void axidma_setup_dma_config(struct xilinx_dma_config *dma_config,
                             struct axidma_chan *chan)
{
    dma_config->dummy = 0;
    return;
}

/* Setup the config structure for VDMA. In 4.x, the VDMA config no longer has
 * vsize, hsize, and stride fields. */
static inline
void axidma_setup_vdma_config(struct xilinx_vdma_config *dma_config, int width,
                              int height, int depth)
{
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

/* Reserve the given channel corresponding to the AXI DMA channel structure. In
 * 4.x, the reservation now goes through the device tree, instead of match. */
static inline
struct dma_chan *axidma_reserve_channel(struct platform_device *pdev,
                                        struct axidma_chan *axidma_chan)
{
    struct dma_chan *chan;

    chan = dma_request_slave_channel(&pdev->dev, axidma_chan->name);
    return (chan == NULL) ? ERR_PTR(-ENODEV) : chan;
}
/*----------------------------------------------------------------------------
 * Linux 3.x Compatibility
 *----------------------------------------------------------------------------*/

#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3,0,0)

#include <linux/dmaengine.h>        // Definitions for DMA structures and types

// Setup the config structure for DMA
static inline
void axidma_setup_dma_config(struct xilinx_dma_config *dma_config,
                             struct axidma_chan *chan)
{
    enum dma_transfer_direction direction;

    direction = axidma_to_dma_dir(chan->dir);
    dma_config->direction = direction;  // Either to memory or from memory
    dma_config->coalesc = 1;            // Interrupt for one transfer completion
    dma_config->delay = 0;              // Disable the delay counter interrupt
    dma_config->reset = 0;              // Don't reset the DMA engine
    return;
}

// Setup the config structure for VDMA
static inline
void axidma_setup_vdma_config(struct xilinx_vdma_config *dma_config, int width,
                              int height, int depth)
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


// Converts the AXI DMA type to its corresponding Xilinx type
static int axidma_to_xilinx_type(enum axidma_type dma_type)
{
    BUG_ON(dma_type != AXIDMA_DMA && dma_type != AXIDMA_VDMA);
    return (dma_type == AXIDMA_DMA) ? XILINX_DMA_IP_DMA : XILINX_DMA_IP_VDMA;
}

// The filter function used to match channels for `dma_request_channel`
static inline
bool axidma_dmadev_filter(struct dma_chan *chan, void *match)
{
    return *(int *)chan->private == (int)match;
}

static inline
int pack_dma_match(int channel_id, enum axidma_type dma_type,
                   enum axidma_dir dma_dir)
{
    int chan_type;
    enum dma_transfer_direction chan_dir;

    chan_type = axidma_to_xilinx_type(dma_type);
    chan_dir  = axidma_to_dma_dir(dma_dir);
    return (channel_id << XILINX_DMA_DEVICE_ID_SHIFT) | (chan_dir & 0xFF) |
           chan_type;
}

// Request the DMA channel that matches the info in the AXI DMA structure
static inline
struct dma_chan *axidma_reserve_channel(struct platform_device *pdev,
                                        struct axidma_chan *axidma_chan)
{
    int match;
    dma_cap_mask_t dma_mask;
    struct dma_chan *chan;

    // Create a capability mask to match against for the DMA
    dma_cap_zero(dma_mask);
    dma_cap_set(DMA_SLAVE | DMA_PRIVATE, dma_mask);

    // Create the match structure, and request the matching channel
    match = pack_dma_match(axidma_chan->channel_id, axidma_chan->type,
                                   axidma_chan->dir);
    chan = dma_request_channel(dma_mask, axidma_dmadev_filter, (void *)match);

    return (chan == NULL) ? ERR_PTR(-ENODEV) : chan;
}

#else

#error("This driver only supports Linux 3.x and 4.x versions. Linux 2.x " \
       "and lower versions are unsupported.")

#endif /* LINUX_VERSION_CODE */

#endif /* VERSION_PORTABILITY_H_ */
