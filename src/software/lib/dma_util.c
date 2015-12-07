/**
 * @file dma_util.c
 * @date Sunday, December 06, 2015 at 06:46:26 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * Contains some simple dma helper utilities
 *
 * @bug No known bugs.
 **/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include <string.h>             // Memory setting and copying
#include <errno.h>              // Error codes

#include "libaxidma.h"          // Interface to AXI DMA
#include "dma_util.h"           // DMA utility definitions

// This function transactions on any channels which are not the input
int do_remainder_transactions(axidma_dev_t dev, int tx_channel,
        int rx_channel, int *chans, int num_chans, int tx_size,
        enum axidma_dir dir, void ***bufs)
{
    int rc;
    int i;
    int buf_size;
    void **buffers;

    // Allocate an array to store the buffers for all of the transactions
    buffers = malloc(num_chans * sizeof(*buffers));
    if (buffers == NULL) {
        return -ENOMEM;
    }
    memset(buffers, 0, num_chans * sizeof(*buffers));

    /* For any remainder channels, start the transactions in case the Tx/Rx
     * pipline has dependencies on them. */
    buf_size = tx_size * BUF_SCALE;
    rc = 0;
    for (i = 0; i < num_chans; i++)
    {
        // If the channel is either the input or output one, we skip it
        if (chans[i] == tx_channel || chans[i] == rx_channel) {
            continue;
        }

        buffers[i] = axidma_malloc(dev, buf_size);
        if (buffers[i] == NULL) {
            fprintf(stderr, "Unable to allocate buffer for remainder "
                    "transaction.\n");
            rc = -ENOMEM;
            break;
        }
        rc = axidma_oneway_transfer(dev, dir, chans[i], buffers[i], buf_size,
                                    false);
        if (rc < 0) {
            axidma_free(dev, buffers[i], buf_size);
            buffers[i] = NULL;
            printf("Warning: Unable to start transaction on channel %d.\n",
                   chans[i]);
        }
    }

    *bufs = buffers;
    return rc;
}

void stop_remainder_transactions(axidma_dev_t dev, int tx_channel,
        int rx_channel, int *chans, int num_chans, int tx_size,
        enum axidma_dir dir, void **bufs)
{
    int i;
    int buf_size;

    if (num_chans == 0) {
        return;
    }

    // Stop the all the remainder transactions, and free their buffers
    buf_size = tx_size * BUF_SCALE;
    for (i = 0; i < num_chans; i++)
    {
        // If the channel is either the input or output one, we skip it
        if (chans[i] == tx_channel || chans[i] == rx_channel) {
            continue;
        }

        // If a buffer is NULL, then we failed with it in do_remainder_trans
        if (bufs[i] == NULL) {
            continue;
        }

        axidma_stop_transfer(dev, chans[i], dir);
        axidma_free(dev, bufs[i], buf_size);
    }

    // Free the buffer array
    free(bufs);
    return;

}

