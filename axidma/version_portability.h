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

/* Handle different versions of linux (mainly DMA definition updates from 3.x to
 * 4.x, such as the header file location moving. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,0,0)

#include <linux/dma/xilinx_dma.h>   // Xilinx DMA config structures (diff path)
#include <linux/dmaengine.h>        // Definitions for DMA structures and types

// DMA_SUCCESS was renamed to DMA_COMPLETE (indicates a DMA transaction is done)
#define DMA_SUCCESS                 DMA_COMPLETE

// The skip destination unmap DMA control option was removed in 4.x
#define DMA_COMPL_SKIP_DEST_UNMAP   0

/* In Linux 4.x, DMA devices now have dedicated functions for terminating
 * outstanding requests and configuring a DMA channel. */
#define dma_terminate_all(dma_dev, dma_chan) \
    ((dma_dev)->device_terminate_all(dma_chan))

/* Setup the config structure for VDMA. In 4.x, the VDMA config no longer has
 * vsize, hsize, and stride fields. */
static inline
void axidma_setup_vdma_config(struct xilinx_vdma_config *dma_config, int width,
                              int height, int depth)
{
    // Silence the compiler, in case this function is not used
    (void)axidma_setup_vdma_config;

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

#else

#include <linux/amba/xilinx_dma.h>  // Xilinx DMA config structures
#include <linux/dmaengine.h>        // Definitions for DMA structures and types

/* In Linux 3.x, DMA devices have a "device_control" function for sending
 * commands to DMA channels (e.g. configuring them or terminating). */
#define dma_terminate_all(dma_dev, dma_chan) \
    ((dma_dev)->device_control(dma_chan, DMA_TERMINATE_ALL, 0))

// Setup the config structure for VDMA
static inline
void axidma_setup_vdma_config(struct xilinx_vdma_config *dma_config, int width,
                              int height, int depth)
{
    // Silence the compiler, in case this function is not used
    (void)axidma_setup_vdma_config;

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

#endif

#endif /* VERSION_PORTABILITY_H_ */
