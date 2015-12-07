/**
 * @file dma_util.h
 * @date Sunday, December 06, 2015 at 06:52:37 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * Contains the interface to some utilities for interacting with DMA.
 *
 * @bug No known bugs.
 **/

#ifndef DMA_UTIL_H_
#define DMA_UTIL_H_

#include "libaxidma.h"

/* We scale all output buffers by 2 of the input size, to ensure no overflow.
 * This is in case the transactions require larger sizes. */
#define BUF_SCALE   2

// Function prototypes
int do_remainder_transactions(axidma_dev_t dev, int tx_channel,
        int rx_channel, int *chans, int num_chans, int tx_size,
        enum axidma_dir dir, void ***bufs);
void stop_remainder_transactions(axidma_dev_t dev, int tx_channel,
        int rx_channel, int *chans, int num_chans, int tx_size,
        enum axidma_dir dir, void **bufs);


#endif /* DMA_UTIL_H_ */
