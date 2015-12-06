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

#include "axidma_ioctl.h"       // The AXI DMA IOCTL interface

// The size of data to send per transfer (1080p image, 7.24 MB)
#define IMAGE_SIZE                  (1920 * 1080)
#define DEFAULT_TRANSFER_SIZE       (IMAGE_SIZE * sizeof(int))

// The default number of transfers to benchmark
#define DEFAULT_NUM_TRANSFERS       1000

// The pattern that we fill into the buffers
#define TEST_PATTERN(i) ((int)(0x1234ACDE ^ (i)))

// Macros to convert integer bytes to double Mb, and vice verse
#define BYTE_TO_MB(size) (((double)(size)) / (1024.0 * 1024.0))
#define MB_TO_BYTE(size) ((size_t)((size) * 1024.0 * 1024.0))

// Converts a tval struct to a double value of the time in seconds
#define TVAL_TO_SEC(tval) \
    (((double)(tval).tv_sec) + (((double)(tval).tv_usec) / 1000000.0))

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
    fprintf(stream, "\t-r <receive transfer size (Mb)>:\tThe size of the data "
            "to receive from the DMA on each transfer. Default is %0.2f Mb.\n",
            default_size);
    fprintf(stream, "\t-t <transmit transfer size (Mb)>:\tThe size of the data "
            "transmit over the DMA on each transfer. Default is %0.2f Mb.\n",
            default_size);
    fprintf(stream, "\t-d <transfer size (Mb)>:\t\tThis option sets the size "
            "of both the data received and transmitted through DMA. Default "
            "is %0.2f Mb.\n", default_size);
    fprintf(stream, "\t-n <number transfers>:\t\t\tThe number of DMA transfers "
            "to perform to do the benchmark. Default is %d transfers.\n",
            DEFAULT_NUM_TRANSFERS);
    return;
}

// Parses the arg string as a double for the given option
static int parse_double(char option, char *arg_str, double *data)
{
    int rc;

    rc = sscanf(optarg, "%lf", data);
    if (rc < 0) {
        perror("Unable to parse argument");
        return rc;
    } else if (rc != 1) {
        fprintf(stderr, "Error: Unable to parse argument '-%c %s' as a "
                "double.\n", option, arg_str);
        print_usage(false);
        return -EINVAL;
    }

    return 0;
}

/* Parses the command line arguments overriding the default transfer sizes,
 * and number of transfer to use for the benchmark if specified. */
static int parse_args(int argc, char **argv, size_t *tx_transfer_size,
                      size_t *rx_transfer_size, int *num_transfers)
{
    double transfer_size;
    int transfers;
    int rc;
    char option;

    // Set the default data size and number of transfers
    *tx_transfer_size = DEFAULT_TRANSFER_SIZE;
    *rx_transfer_size = DEFAULT_TRANSFER_SIZE;
    *num_transfers = DEFAULT_NUM_TRANSFERS;

    while ((option = getopt(argc, argv, "hd:r:t:n:")) != (char)-1)
    {
        switch (option)
        {
            // Parse the transfer size argument, sets both tx and rx size
            case 'd':
                if (parse_double(option, optarg, &transfer_size) < 0) {
                    return -EINVAL;
                }
                // Convert the size to bytes
                *tx_transfer_size = MB_TO_BYTE(transfer_size);
                *rx_transfer_size = MB_TO_BYTE(transfer_size);
                break;

            // Parse the transmit transfer size argument
            case 't':
                if (parse_double(option, optarg, &transfer_size) < 0) {
                    return -EINVAL;
                }
                *tx_transfer_size = MB_TO_BYTE(transfer_size);
                break;

            // Parse the receive transfer size argument
            case 'r':
                if (parse_double(option, optarg, &transfer_size) < 0) {
                    return -EINVAL;
                }
                *rx_transfer_size = MB_TO_BYTE(transfer_size);
                break;

            // Parse the number of transfers argument
            case 'n':
                rc = sscanf(optarg, "%d", &transfers);
                if (rc < 0) {
                    perror("Unable to parse argument");
                    return rc;
                } else if (rc != 1) {
                    fprintf(stderr, "Error: Unable to parse argument '-%c %s' "
                            "as an integer.\n", option, optarg);
                    print_usage(false);
                    return -EINVAL;
                }
                *num_transfers = transfers;
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

    return 0;
}

// Finds the transmit and receive channels for the transaction
static int find_dma_channels(int axidma_fd, int *tx_channel, int *rx_channel)
{
    int rc, i;
    struct axidma_chan *channels, *chan;
    struct axidma_num_channels num_chan;
    struct axidma_channel_info channel_info;

    // Find the number of channels and allocate a buffer to hold their data
    rc = ioctl(axidma_fd, AXIDMA_GET_NUM_DMA_CHANNELS, &num_chan);
    if (rc < 0) {
        perror("Unable to get the number of DMA channels");
        return rc;
    } else if (num_chan.num_channels == 0) {
        fprintf(stderr, "No DMA channels are present.\n");
        return -ENODEV;
    }

    // Get the metdata about all the available channels
    channels = malloc(num_chan.num_channels * sizeof(*channels));
    if (channels == NULL) {
        fprintf(stderr, "Unable to allocate channel information buffer.\n");
        return -ENOMEM;
    }
    channel_info.channels = channels;
    rc = ioctl(axidma_fd, AXIDMA_GET_DMA_CHANNELS, &channel_info);
    if (rc < 0) {
        perror("Unable to get DMA channel information");
        free(channels);
        return rc;
    }

    // Search for the first available transmit and receive DMA channels
    *tx_channel = -1;
    *rx_channel = -1;
    for (i = 0; i < num_chan.num_channels; i++)
    {
        chan = &channel_info.channels[i];
        if (chan->dir == AXIDMA_WRITE && chan->type == AXIDMA_DMA) {
            *tx_channel = chan->channel_id;
            break;
        }
    }
    for (i = 0; i < num_chan.num_channels; i++)
    {
        chan = &channel_info.channels[i];
        if (chan->dir == AXIDMA_READ && chan->type == AXIDMA_DMA) {
            *rx_channel = chan->channel_id;
            break;
        }
    }

    if (*tx_channel == -1) {
        fprintf(stderr, "No transmit DMA channels are present.\n");
        free(channels);
        return -ENODEV;
    }
    if (*rx_channel == -1) {
        fprintf(stderr, "No receive DMA channels are present.\n");
        free(channels);
        return -ENODEV;
    }

    free(channels);
    return 0;
}

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

/* Profiles the transfer and receive rates for the DMA, reporting the throughput
 * of each channel in MB/s. */
static int time_dma(int axidma_fd, struct axidma_inout_transaction *trans,
                    int num_transfers)
{
    int i;
    struct timeval start_time, end_time;
    double elapsed_time, tx_data_rate, rx_data_rate;

    // Begin timing
    gettimeofday(&start_time, NULL);

    // Perform n transfers
    for (i = 0; i < num_transfers; i++)
    {
        if (ioctl(axidma_fd, AXIDMA_DMA_READWRITE, trans) < 0) {
            perror("Failed to peform a read write DMA transaction");
            fprintf(stderr, "DMA failed on transfer %d, not reporting timing "
                    "results.\n", i+1);
            return -1;
        }
    }

    // End timing
    gettimeofday(&end_time, NULL);

    // Compute the throughput of each channel
    elapsed_time = TVAL_TO_SEC(end_time) - TVAL_TO_SEC(start_time);
    tx_data_rate = BYTE_TO_MB(trans->tx_buf_len) * num_transfers / elapsed_time;
    rx_data_rate = BYTE_TO_MB(trans->rx_buf_len) * num_transfers / elapsed_time;

    // Report the statistics to the user
    printf("DMA Timing Statistics:\n");
    printf("\tElapsed Time: %0.2f s\n", elapsed_time);
    printf("\tTransmit Throughput: %0.2f Mb/s\n", tx_data_rate);
    printf("\tReceive Throughput: %0.2f Mb/s\n", rx_data_rate);
    printf("\tTotal Throughput: %0.2f Mb/s\n", tx_data_rate + rx_data_rate);

    return 0;
}

int main(int argc, char **argv)
{
    int rc, fd;
    int num_transfers;
    int tx_channel, rx_channel;
    size_t tx_transfer_size, rx_transfer_size;
    char *tx_buf, *rx_buf;
    struct axidma_inout_transaction trans;

    // Check if the user overrided the default transfer size and number
    if (parse_args(argc, argv, &tx_transfer_size, &rx_transfer_size,
                   &num_transfers) < 0) {
        rc = -1;
        goto ret;
    }
    printf("AXI DMA Benchmark Parameters:\n");
    printf("\tTransmit Buffer Size: %0.2f Mb\n", BYTE_TO_MB(tx_transfer_size));
    printf("\tReceive Buffer Size: %0.2f Mb\n", BYTE_TO_MB(rx_transfer_size));
    printf("\tNumber of DMA Transfers: %d transfers\n\n", num_transfers);

    // Open the AXI dma device, initializing anything necessary
    fd = open("/dev/axidma", O_RDWR|O_EXCL);
    if (fd < 0) {
        perror("Error opening AXI DMA device");
        rc = -1;
        goto ret;
    }

    // Use the lowest numbered DMA channels for the transaction
    if (find_dma_channels(fd, &tx_channel, &rx_channel) < 0) {
        rc = -1;
        goto close_axidma;
    }

    // Map memory regions for the transmit and receive buffers
    tx_buf = mmap(NULL, tx_transfer_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd,
                  (off_t)0);
    if (tx_buf == MAP_FAILED) {
        perror("Unable to mmap memory region from AXI DMA device");
        rc = -1;
        goto close_axidma;
    }
    rx_buf = mmap(NULL, rx_transfer_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd,
                  (off_t)0);
    if (rx_buf == MAP_FAILED) {
        perror("Unable to mmap memory region from AXI DMA device");
        rc = -1;
        goto free_tx_buf;
    }

    // Initialize the buffer region we're going to transmit
    init_data(tx_buf, rx_buf, tx_transfer_size, rx_transfer_size);

    // Transmit the buffer to DMA a single time
    trans.wait = true;
    trans.tx_channel_id = tx_channel;
    trans.tx_buf = tx_buf;
    trans.tx_buf_len = tx_transfer_size;
    trans.rx_channel_id = rx_channel;
    trans.rx_buf = rx_buf;
    trans.rx_buf_len = rx_transfer_size;
    if (ioctl(fd, AXIDMA_DMA_READWRITE, &trans) < 0) {
        perror("Failed to peform a read write DMA transaction");
        rc = -1;
        goto free_rx_buf;
    }

    // Verify that the data makes sense after the transfer
    if (!verify_data(tx_buf, rx_buf, tx_transfer_size, rx_transfer_size)) {
        rc = -1;
        goto free_rx_buf;
    }
    printf("Single transfer test successfully completed!\n");

    // Time the DMA eingine
    printf("Beginning performance analysis of the DMA engine.\n\n");
    if (time_dma(fd, &trans, num_transfers) < 0) {
        rc = -1;
        goto free_rx_buf;
    }

    rc = 0;

free_rx_buf:
    munmap(rx_buf, rx_transfer_size);
free_tx_buf:
    munmap(tx_buf, tx_transfer_size);
close_axidma:
    close(fd);
ret:
    return rc;
}

