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
#include <linux/dmaengine.h>        // DMA types and functions
#include <linux/slab.h>             // Allocation functions (ioremap)

// Local dependencies
#include "axidma.h"

/*----------------------------------------------------------------------------
 * DMA Operations
 *----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------
 * Initialization and Cleanup
 *----------------------------------------------------------------------------*/

static bool axidma_dmadev_filter(struct dma_chan *chan, void *match)
{
    return *(int *)chan->private == (int)match;
}

int axidma_dma_init(struct axidma_device *dev)
{
    unsigned long cma_base;
    dma_cap_mask_t dma_mask;
    int tx_chan_match, rx_chan_match;
    int device_id;
    int rc;

    // Calculate the base of the CMA region
    cma_base = dev->mem_size - dev->cma_len;
    dev->dma_base_paddr = (void *)(dev->mem_size - dev->cma_len);

    // Remap the contigous DMA region into the kernel address space
    dev->dma_base_vaddr = ioremap_nocache(cma_base, dev->cma_len);
    if (dev->dma_base_vaddr == NULL) {
        axidma_err("Unable to allocate contiguous memory region at %p of size "
                   "%lu.\n", dev->dma_base_paddr, dev->cma_len);
        rc = -ENOMEM;
        goto ret;
    }

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
        goto unmap_io;
    }
    dev->rx_chan = dma_request_channel(dma_mask, axidma_dmadev_filter,
                                       (void *)rx_chan_match);
    if (dev->rx_chan == NULL) {
        axidma_err("Could not find a receive channel.\n");
        rc = -ENODEV;
        goto cleanup_tx;
    }

    // Inform the user of the DMA information
    axidma_info("Allocated contigous memory region at 0x%08lx of size %lu.\n",
                (unsigned long)dev->dma_base_paddr, dev->cma_len);
    axidma_info("Found %d receive channels and %d transmit channels.\n", 1, 1);
    return 0;

cleanup_tx:
    dma_release_channel(dev->tx_chan);
unmap_io:
    iounmap(dev->dma_base_vaddr);
ret:
    return rc;
}

void axidma_dma_exit(struct axidma_device *dev)
{
    // TODO: Check if any dma transactions are running

    // Cleanup all DMA related structures
    iounmap(dev->dma_base_vaddr);
    dma_release_channel(dev->tx_chan);
    dma_release_channel(dev->rx_chan);
}

