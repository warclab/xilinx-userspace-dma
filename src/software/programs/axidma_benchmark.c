/**
 * @file axidma_benchmark.c
 * @date Thursday, November 26, 2015 at 10:29:26 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This is a simple program that benchmarks the AXI DMA transfer rate for
 * whatever logic is sitting on the PL fabric. It sends some data out over the
 * PL fabric via AXI DMA to whatever is sitting there, and waits to receive
 * some data back from the PL fabric.
 *
 * The program first runs a single transfer to verify that the DMA works
 * properly, then profiles the DMA engine. The program sends out a specific
 * transfer size, and gets back a potentially different receive size. It runs
 * the a given number of times to calculate the performance statistics. All of
 * these options are configurable from the command line.
 *
 * The program sends a transfer out of the given size, and receives it into a
 * buffer of the same size. It first sends a single transfer to test that this
 * can complete successfully. It then
 *
 * @bug No known bugs.
 **/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <sys/mman.h>           // Mmap system call
#include <sys/ioctl.h>          // IOCTL system call
#include <unistd.h>             // Close() system call
#include <sys/time.h>           // Timing functions and definitions
#include <getopt.h>             // Option parsing
#include <errno.h>              // Error codes

#include "libaxidma.h"          // Interface to the AXI DMA
#include "util.h"               // Miscellaneous utilities
#include "dma_util.h"           // DMA Utilities
#include "conversion.h"         // Miscellaneous conversion utilities

/*----------------------------------------------------------------------------
 * Internal Definitons
 *----------------------------------------------------------------------------*/

// The size of data to send per transfer (1080p image, 7.24 MB)
#define IMAGE_SIZE                  (1920 * 1080)
#define DEFAULT_TRANSFER_SIZE       (IMAGE_SIZE * sizeof(int))

// The default number of transfers to benchmark
#define DEFAULT_NUM_TRANSFERS       1000

// The pattern that we fill into the buffers
#define TEST_PATTERN(i) ((int)(0x1234ACDE ^ (i)))

// The DMA context passed to the helper thread, who handles remainder channels

/*----------------------------------------------------------------------------
 * Command-line Interface
 *----------------------------------------------------------------------------*/

// Prints the usage for this program
static void print_usage(bool help)
{
    FILE* stream = (help) ? stdout : stderr;
    double default_size;

    fprintf(stream, "Usage: axidma_benchmark [-d <transfer size (Mb)>] [-n "
            "<number transfers>].\n");
    if (!help) {
        return;
    }

    default_size = BYTE_TO_MB(DEFAULT_TRANSFER_SIZE);
    fprintf(stream, "\t-t <DMA tx channel>:\t\t\tThe device id of the DMA "
            "channel to use for transmitting the data to the PL fabric.\n");
    fprintf(stream, "\t-r <DMA rx channel>:\t\t\tThe device id of the DMA "
            "channel to use for receiving the the data from the PL fabric.\n");
    fprintf(stream, "\t-i <transmit transfer size (MB)>:\tThe size of the data "
            "transmit over the DMA on each transfer. Default is %0.2f MB.\n",
            default_size);
    fprintf(stream, "\t-b <transmit transfer size (bytes)>:\tThe size of the "
            "data transmit over the DMA on each transfer. Default is %d "
            "bytes.\n", DEFAULT_TRANSFER_SIZE);
    fprintf(stream, "\t-o <receive transfer size (MB)>:\tThe size of the data "
            "to receive from the DMA on each transfer. Default is %0.2f MB.\n",
            default_size);
    fprintf(stream, "\t-s <receive transfer size (bytes)>:\tThe size of the "
            "data to receive from the DMA on each transfer. Default is %d "
            "bytes.\n", DEFAULT_TRANSFER_SIZE);
    fprintf(stream, "\t-n <number transfers>:\t\t\tThe number of DMA transfers "
            "to perform to do the benchmark. Default is %d transfers.\n",
            DEFAULT_NUM_TRANSFERS);
    return;
}

/* Parses the command line arguments overriding the default transfer sizes,
 * and number of transfer to use for the benchmark if specified. */
static int parse_args(int argc, char **argv, int *tx_channel, int *rx_channel,
        size_t *tx_size, size_t *rx_size, int *num_transfers)
{
    double double_arg;
    int int_arg;
    char option;

    // Set the default data size and number of transfers
    *tx_channel = -1;
    *rx_channel = -1;
    *tx_size = DEFAULT_TRANSFER_SIZE;
    *rx_size = DEFAULT_TRANSFER_SIZE;
    *num_transfers = DEFAULT_NUM_TRANSFERS;

    while ((option = getopt(argc, argv, "t:r:i:b:o:s:n:h")) != (char)-1)
    {
        switch (option)
        {
            // Parse the transmit channel argument
            case 't':
                if (parse_int(option, optarg, &int_arg) < 0) {
                    print_usage(false);
                    return -EINVAL;
                }
                *tx_channel = int_arg;
                break;

            // Parse the transmit transfer size argument
            case 'r':
                if (parse_int(option, optarg, &int_arg) < 0) {
                    print_usage(false);
                    return -EINVAL;
                }
                *rx_channel = int_arg;
                break;

            // Parse the transmit transfer size argument
            case 'i':
                if (parse_double(option, optarg, &double_arg) < 0) {
                    print_usage(false);
                    return -EINVAL;
                }
                *tx_size = MB_TO_BYTE(double_arg);
                break;

            // Parse the transmit transfer size argument
            case 'b':
                if (parse_int(option, optarg, &int_arg) < 0) {
                    print_usage(false);
                    return -EINVAL;
                }
                *tx_size = int_arg;
                break;

            // Parse the receive transfer size argument
            case 'o':
                if (parse_double(option, optarg, &double_arg) < 0) {
                    print_usage(false);
                    return -EINVAL;
                }
                *rx_size = MB_TO_BYTE(double_arg);
                break;

            // Parse the receive transfer size argument
            case 's':
                if (parse_int(option, optarg, &int_arg) < 0) {
                    print_usage(false);
                    return -EINVAL;
                }
                *rx_size = int_arg;
                break;

            // Parse the number of transfers argument
            case 'n':
                if (parse_int(option, optarg, &int_arg) < 0) {
                    print_usage(false);
                    return -EINVAL;
                }
                *num_transfers = int_arg;
                break;

            // Print detailed usage message
            case 'h':
                print_usage(true);
                exit(0);

            default:
                print_usage(false);
                return -EINVAL;
        }
    }

    if ((*tx_channel == -1) ^ (*rx_channel == -1)) {
        fprintf(stderr, "Error: If one of -r/-t is specified, then both must "
                "be.\n");
        return -EINVAL;
    }

    if ((*tx_size == DEFAULT_TRANSFER_SIZE) ^
        (*rx_size == DEFAULT_TRANSFER_SIZE)) {
        fprintf(stderr, "Error: If one of -i/-b or -o/-s is specified, then "
                "both most be.\n");
        return -EINVAL;
    }


    return 0;
}

/*----------------------------------------------------------------------------
 * Verification Test
 *----------------------------------------------------------------------------*/

/* Initialize the two buffers, filling buffers with a preset but "random"
 * pattern. */
static void init_data(char *tx_buf, char *rx_buf, size_t tx_buf_size,
                      size_t rx_buf_size)
{
    size_t i;
    int *transmit_buffer, *receive_buffer;

    transmit_buffer = (int *)tx_buf;
    receive_buffer = (int *)rx_buf;

    // Fill the buffer with integer patterns
    for (i = 0; i < tx_buf_size / sizeof(int); i++)
    {
        transmit_buffer[i] = TEST_PATTERN(i);
    }

    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < tx_buf_size % sizeof(int); i++)
    {
        tx_buf[i] = TEST_PATTERN(i + tx_buf_size / sizeof(int));
    }

    // Fill the buffer with integer patterns
    for (i = 0; i < rx_buf_size / sizeof(int); i++)
    {
        receive_buffer[i] = TEST_PATTERN(i + tx_buf_size);
    }

    // Fill in any leftover bytes if it's not aligned
    for (i = 0; i < rx_buf_size % sizeof(int); i++)
    {
        rx_buf[i] = TEST_PATTERN(i + tx_buf_size + rx_buf_size / sizeof(int));
    }

    return;
}

/* Verify the two buffers. For transmit, verify that it is unchanged. For
 * receive, we don't know the PL fabric function, so the best we can
 * do is check if it changed and warn the user if it is not. */
static bool verify_data(char *tx_buf, char *rx_buf, size_t tx_buf_size,
                        size_t rx_buf_size)
{
    int *transmit_buffer, *receive_buffer;
    size_t i, rx_data_same, rx_data_units;
    double match_fraction;

    transmit_buffer = (int *)tx_buf;
    receive_buffer = (int *)rx_buf;

    // Verify words in the transmit buffer
    for (i = 0; i < tx_buf_size / sizeof(int); i++)
    {
        if (transmit_buffer[i] != TEST_PATTERN(i)) {
            fprintf(stderr, "Test failed! The transmit buffer was overwritten "
                    "at byte %zu.\n", i);
            fprintf(stderr, "Expected 0x%08x, found 0x%08x.\n", TEST_PATTERN(i),
                    tx_buf[i]);
            return false;
        }
    }

    // Verify any leftover bytes in the buffer
    for (i = 0; i < tx_buf_size % sizeof(int); i++)
    {
        if (tx_buf[i] != TEST_PATTERN(i + tx_buf_size / sizeof(int))) {
            fprintf(stderr, "Test failed! The transmit buffer was overwritten "
                    "at byte %zu.\n", i);
            fprintf(stderr, "Expected 0x%08x, found 0x%08x.\n", TEST_PATTERN(i),
                    tx_buf[i]);
            return false;
        }
    }

    // Verify words in the receive buffer
    rx_data_same = 0;
    for (i = 0; i < rx_buf_size / sizeof(int); i++)
    {
        if (receive_buffer[i] == TEST_PATTERN(i+tx_buf_size)) {
            rx_data_same += 1;
        }
    }

    // Verify any leftover bytes in the buffer
    for (i = 0; i < rx_buf_size % sizeof(int); i++)
    {
        if (rx_buf[i] == TEST_PATTERN(i+tx_buf_size+rx_buf_size/sizeof(int))) {
            rx_data_same += 1;
        }
    }

    // Warn the user if more than 10% of the pixels match the test pattern
    rx_data_units = rx_buf_size / sizeof(int) + rx_buf_size % sizeof(int);
    if (rx_data_same == rx_data_units) {
        fprintf(stderr, "Test Failed! The receive buffer was not updated.\n");
        return false;
    } else if (rx_data_same >= rx_data_units / 10) {
        match_fraction = ((double)rx_data_same) / ((double)rx_data_units);
        printf("Warning: %0.2f%% of the receive buffer matches the "
               "initialization pattern.\n", match_fraction * 100.0);
        printf("This may mean that the receive buffer was not properly "
               "updated.\n");
    }

    return true;
}

static int single_transfer_test(axidma_dev_t dev, int tx_channel, void *tx_buf,
        int tx_size, int rx_channel, void *rx_buf, int rx_size)
{
    int rc;

    // Initialize the buffer region we're going to transmit
    init_data(tx_buf, rx_buf, tx_size, rx_size);

    /* Start all the remainder Tx and Rx transaction in case the main
     * transaction has any dependencies with them. */
    rc = start_remainder_transactions(dev, tx_channel, rx_channel, tx_size);
    if (rc < 0) {
        fprintf(stderr, "Unable to start remainder transactions.\n");
        goto stop_rem;
    }

    // Perform the main transaction
    rc = axidma_twoway_transfer(dev, tx_channel, tx_buf, tx_size,
            rx_channel, rx_buf, rx_size, true);
    if (rc < 0) {
        goto stop_rem;
    }

    // Verify that the data in the buffer changed
    if (!verify_data(tx_buf, rx_buf, tx_size, rx_size) < 0) {
        rc = -EINVAL;
        goto stop_rem;
    }
    rc = 0;

    // Stop all the remainder transactions
stop_rem:
    stop_remainder_transactions(dev, tx_channel, rx_channel, tx_size);

    return rc;
}


/*----------------------------------------------------------------------------
 * Benchmarking Test
 *----------------------------------------------------------------------------*/

/* Profiles the transfer and receive rates for the DMA, reporting the throughput
 * of each channel in MB/s. */
static int time_dma(axidma_dev_t dev, int tx_channel, void *tx_buf, int tx_size,
        int rx_channel, void *rx_buf, int rx_size, int num_transfers)
{
    int i, rc;
    struct timeval start_time, end_time;
    double elapsed_time, tx_data_rate, rx_data_rate;

    // Begin timing
    gettimeofday(&start_time, NULL);

    /* Start all the remainder Tx and Rx transaction in case the main
     * transaction has any dependencies with them. */
    rc = start_remainder_transactions(dev, tx_channel, rx_channel, tx_size);
    if (rc < 0) {
        fprintf(stderr, "Unable to start remainder transactions.\n");
        goto stop_rem;
    }

    // Perform n transfers
    for (i = 0; i < num_transfers; i++)
    {
        rc = axidma_twoway_transfer(dev, tx_channel, tx_buf, tx_size,
                rx_channel, rx_buf, rx_size, true);
        if (rc < 0) {
            fprintf(stderr, "DMA failed on transfer %d, not reporting timing "
                    "results.\n", i+1);
            goto stop_rem;
        }

        rc = dispatch_remainder_transactions(dev, tx_channel, rx_channel,
                                             tx_size);
        if (rc < 0) {
            fprintf(stderr, "Failed to disptach remainder transactions on "
                    "transfer %d, not reporting timing results.\n", i+1);
            goto stop_rem;
        }
    }

    // End timing
    gettimeofday(&end_time, NULL);

    // Compute the throughput of each channel
    elapsed_time = TVAL_TO_SEC(end_time) - TVAL_TO_SEC(start_time);
    tx_data_rate = BYTE_TO_MB(tx_size) * num_transfers / elapsed_time;
    rx_data_rate = BYTE_TO_MB(rx_size) * num_transfers / elapsed_time;

    // Report the statistics to the user
    printf("DMA Timing Statistics:\n");
    printf("\tElapsed Time: %0.2f s\n", elapsed_time);
    printf("\tTransmit Throughput: %0.2f Mb/s\n", tx_data_rate);
    printf("\tReceive Throughput: %0.2f Mb/s\n", rx_data_rate);
    printf("\tTotal Throughput: %0.2f Mb/s\n", tx_data_rate + rx_data_rate);

    rc = 0;

stop_rem:
    stop_remainder_transactions(dev, tx_channel, rx_channel, tx_size);
    return rc;
}

/*----------------------------------------------------------------------------
 * Main Function
 *----------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    int rc;
    int num_transfers;
    int tx_channel, rx_channel;
    int num_rx, num_tx;
    size_t tx_size, rx_size;
    char *tx_buf, *rx_buf;
    int *tx_chans, *rx_chans;
    axidma_dev_t axidma_dev;

    // Check if the user overrided the default transfer size and number
    if (parse_args(argc, argv, &tx_channel, &rx_channel, &tx_size, &rx_size,
                   &num_transfers) < 0) {
        rc = 1;
        goto ret;
    }
    printf("AXI DMA Benchmark Parameters:\n");
    printf("\tTransmit Buffer Size: %0.2f Mb\n", BYTE_TO_MB(tx_size));
    printf("\tReceive Buffer Size: %0.2f Mb\n", BYTE_TO_MB(rx_size));
    printf("\tNumber of DMA Transfers: %d transfers\n\n", num_transfers);

    // Initialize the AXI DMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL) {
        fprintf(stderr, "Failed to initialize the AXI DMA device.\n");
        rc = 1;
        goto ret;
    }

    // Map memory regions for the transmit and receive buffers
    tx_buf = axidma_malloc(axidma_dev, tx_size);
    if (tx_buf == NULL) {
        perror("Unable to allocatememory region from AXI DMA device");
        rc = -1;
        goto destroy_axidma;
    }
    rx_buf = axidma_malloc(axidma_dev, rx_size);
    if (rx_buf == MAP_FAILED) {
        perror("Unable to allocate memory region from AXI DMA device");
        rc = -1;
        goto free_tx_buf;
    }

    // Get all the transmit and receive channels
    tx_chans = axidma_get_dma_tx(axidma_dev, &num_tx);
    if (num_tx < 1) {
        fprintf(stderr, "Error: No transmit channels were found.\n");
        return -ENODEV;
    }
    rx_chans = axidma_get_dma_rx(axidma_dev, &num_rx);
    if (num_rx < 1) {
        fprintf(stderr, "Error: No receive channels were found.\n");
        return -ENODEV;
    }

    /* If the user didn't specify the channels, we assume that the transmit and
     * receive channels are the lowest numbered ones. */
    if (tx_channel == -1 && rx_channel == -1) {
        tx_channel = tx_chans[0];
        rx_channel = rx_chans[0];
    }
    printf("Using transmit channel %d and receive channel %d.\n", tx_channel,
           rx_channel);

    // Transmit the buffer to DMA a single time
    rc = single_transfer_test(axidma_dev, tx_channel, tx_buf, tx_size,
                              rx_channel, rx_buf, rx_size);
    if (rc < 0) {
        rc = 1;
        goto free_rx_buf;
    }
    printf("Single transfer test successfully completed!\n");


    // Time the DMA eingine
    printf("Beginning performance analysis of the DMA engine.\n\n");
    if (time_dma(axidma_dev, tx_channel, tx_buf, tx_size, rx_channel,
                 rx_buf, rx_size, num_transfers) < 0) {
        rc = 1;
        goto free_rx_buf;
    }

    rc = 0;

free_rx_buf:
    axidma_free(axidma_dev, rx_buf, rx_size);
free_tx_buf:
    axidma_free(axidma_dev, tx_buf, tx_size);
destroy_axidma:
    axidma_destroy(axidma_dev);
ret:
    return rc;
}
