/**
 * @file axidma_transfer.c
 * @date Sunday, November 29, 2015 at 12:23:43 PM EST
 * @author Brandon Perez (bmperez)
 * @author Jared Choi (jaewonch)
 *
 * This program performs a simple AXI DMA transfer. It takes the input file,
 * loads it into memory, and then sends it out over the PL fabric. It then
 * receives the data back, and places it into the given output file.
 *
 * By default it uses the lowest numbered channels for the transmit and receive,
 * unless overriden by the user. The amount of data transfered is automatically
 * determined from the file size. Unless specified, the output file size is
 * made to be 10 times the input size (to account for creating more data).
 *
 * This program also handles any additional channels that the pipeline
 * on the PL fabric might depend on. It starts up DMA transfers for these
 * pipeline stages, and discards their results.
 *
 * @bug No known bugs.
 **/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <unistd.h>             // Close() system call
#include <string.h>             // Memory setting and copying
#include <getopt.h>             // Option parsing
#include <errno.h>              // Error codes

#include "util.h"               // Miscellaneous utilities
#include "byte_conversion.h"    // Convert bytes to MBs
#include "libaxidma.h"          // Interface ot the AXI DMA library

/*----------------------------------------------------------------------------
 * Global Definitions
 *----------------------------------------------------------------------------*/

// We scale all output buffers by 2 of the input size, to ensure no overflow
#define BUF_SCALE   2

/*----------------------------------------------------------------------------
 * Command Line Interface
 *----------------------------------------------------------------------------*/

// Prints the usage for this program
static void print_usage(bool help)
{
    FILE* stream = (help) ? stdout : stderr;

    fprintf(stream, "Usage: axidma_transfer <input path> <output path> "
            "[-t <DMA tx channel>] [-r <DMA rx channel>] [-s <Output file size>"
            " | -o <Output file size>].\n");
    if (!help) {
        return;
    }

    fprintf(stream, "\t<input path>:\t\tThe path to file to send out over AXI "
            "DMA to the PL fabric. Can be a relative or absolute path.\n");
    fprintf(stream, "\t<output path>:\t\tThe path to place the received data "
            "from the PL fabric into. Can be a relative or absolute path.\n");
    fprintf(stream, "\t-t <DMA tx channel>:\tThe device id of the DMA channel "
            "to use for transmitting the file. Default is to use the lowest "
            "numbered channel available.\n");
    fprintf(stream, "\t-r <DMA rx channel>:\tThe device id of the DMA channel "
            "to use for receiving the data from the PL fabric. Default is to "
            "use the lowest numbered channel available.\n");
    fprintf(stream, "\t-s <Output file size>:\tThe size of the output file in "
            "bytes. This is an integer value that must be at least the number "
            "of bytes received back. By default, this is 10 times the size of "
            "the input file.\n");
    fprintf(stream, "\t-o <Output file size>:\tThe size of the output file in "
            "Mbs. This is a floating-point value that must be at least the "
            "number of bytes received back. By default, this is the same "
            "the size of the input file.\n");
    return;
}

/* Parses the command line arguments overriding the default transfer sizes,
 * and number of transfer to use for the benchmark if specified. */
static int parse_args(int argc, char **argv, char **input_path,
    char **output_path, int *tx_channel, int *rx_channel, int *output_size)
{
    char option;
    int int_arg;
    double double_arg;
    bool o_specified, s_specified;
    int rc;

    // Set the default values for the arguments
    *tx_channel = -1;
    *rx_channel = -1;
    *output_size = -1;
    o_specified = false;
    s_specified = false;
    rc = 0;

    while ((option = getopt(argc, argv, "t:r:s:o:h")) != (char)-1)
    {
        switch (option)
        {
            // Parse the transmit channel device id
            case 't':
                rc = parse_int(option, optarg, &int_arg);
                if (rc < 0) {
                    print_usage(false);
                    return rc;
                }
                *tx_channel = int_arg;
                break;

            // Parse the receive channel device id
            case 'r':
                rc = parse_int(option, optarg, &int_arg);
                if (rc < 0) {
                    print_usage(false);
                    return rc;
                }
                *rx_channel = int_arg;
                break;

            // Parse the output file size (in bytes)
            case 's':
                rc = parse_int(option, optarg, &int_arg);
                if (rc < 0) {
                    print_usage(false);
                    return rc;
                }
                *output_size = int_arg;
                s_specified = true;
                break;

            // Parse the output file size (in MBs)
            case 'o':
                rc = parse_double(option, optarg, &double_arg);
                if (rc < 0) {
                    print_usage(false);
                    return rc;
                }
                *output_size = MB_TO_BYTE(double_arg);
                o_specified = true;
                break;

            case 'h':
                print_usage(true);
                exit(0);

            default:
                print_usage(false);
                return -EINVAL;
        }
    }

    // If one of -t or -r is specified, then both must be
    if ((*tx_channel == -1) ^ (*rx_channel == -1)) {
        fprintf(stderr, "Error: Either both -t and -r must be specified, or "
                "neither.\n");
        print_usage(false);
        return -EINVAL;
    }

    // Only one of -s and -o can be specified
    if (s_specified && o_specified) {
        fprintf(stderr, "Error: Only one of -s and -o can be specified.\n");
        print_usage(false);
        return -EINVAL;
    }

    // Check that there are enough command line arguments
    if (optind > argc-2) {
        fprintf(stderr, "Error: Too few command line arguments.\n");
        print_usage(false);
        return -EINVAL;
    }

    // Check if there are too many command line arguments remaining
    if (optind < argc-2) {
        fprintf(stderr, "Error: Too many command line arguments.\n");
        print_usage(false);
        return -EINVAL;
    }

    // Parse out the input and output paths
    *input_path = argv[optind];
    *output_path = argv[optind+1];
    return 0;
}

/*----------------------------------------------------------------------------
 * DMA File Transfer Functions
 *----------------------------------------------------------------------------*/

static int do_remainder_transactions(axidma_dev_t dev, int input_channel,
        int output_channel, int *chans, int num_chans, int input_size,
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
    buf_size = input_size * BUF_SCALE;
    rc = 0;
    for (i = 0; i < num_chans; i++)
    {
        // If the channel is either the input or output one, we skip it
        if (chans[i] == input_channel || chans[i] == output_channel) {
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

static void stop_remainder_transactions(axidma_dev_t dev, int input_channel,
        int output_channel, int *chans, int num_chans, int input_size,
        enum axidma_dir dir, void **bufs)
{
    int i;
    int buf_size;

    if (num_chans == 0) {
        return;
    }

    // Stop the all the remainder transactions, and free their buffers
    buf_size = input_size * BUF_SCALE;
    for (i = 0; i < num_chans; i++)
    {
        // If a buffer is NULL, then we stopped early in do_remainder_trans
        if (bufs[i] == NULL) {
            break;
        }

        // If the channel is either the input or output one, we skip it
        if (chans[i] == input_channel || chans[i] == output_channel) {
            continue;
        }

        axidma_stop_transfer(dev, chans[i], dir);
        axidma_free(dev, bufs[i], buf_size);
    }

    // Free the buffer array
    free(bufs);
    return;

}

static int do_transfer(axidma_dev_t dev, int input_channel, void *input_buf,
        int input_size, int output_channel, void *output_buf, int output_size,
        int *tx_chans, int num_tx, int *rx_chans, int num_rx)
{
    int rc;
    void **tx_bufs, **rx_bufs;

    /* Start all the remainder Tx and Rx transaction in case the main
     * transaction has any dependencies with them. */
    rc = do_remainder_transactions(dev, input_channel, output_channel, tx_chans,
            num_tx, input_size, AXIDMA_WRITE, &tx_bufs);
    if (rc < 0) {
        goto stop_rem_tx;
    }
    rc = do_remainder_transactions(dev, input_channel, output_channel, rx_chans,
            num_rx, input_size, AXIDMA_READ, &rx_bufs);
    if (rc < 0) {
        goto stop_rem_rx;
    }

    // Perform the main transaction
    rc = axidma_twoway_transfer(dev, input_channel, input_buf, input_size,
           output_channel, output_buf, output_size, true);
    if (rc < 0) {
        fprintf(stderr, "DMA read write transaction failed.\n");
    }

    // Stop the remainder transactions, and free their memory
stop_rem_rx:
    stop_remainder_transactions(dev, input_channel, output_channel, rx_chans,
            num_rx, input_size, AXIDMA_READ, rx_bufs);
stop_rem_tx:
    stop_remainder_transactions(dev, input_channel, output_channel,
            tx_chans, num_tx, input_size, AXIDMA_WRITE, tx_bufs);
    return rc;
}

static int transfer_file(axidma_dev_t dev, int input_fd, int input_channel,
        int input_size, int output_fd, int output_channel, int output_size,
        char *output_path)
{
    int rc;
    void *input_buf, *output_buf;
    int *tx_chans, *rx_chans;
    int num_tx, num_rx;

    // Allocate a buffer for the input file, and read it into the buffer
    input_buf = axidma_malloc(dev, input_size);
    if (input_buf == NULL) {
        fprintf(stderr, "Failed to allocate the input buffer.\n");
        rc = -ENOMEM;
        goto ret;
    }
    rc = robust_read(input_fd, input_buf, input_size);
    if (rc < 0) {
        perror("Unable to read in input buffer.\n");
        axidma_free(dev, input_buf, input_size);
        return rc;
    }

    // Allocate a buffer for the output file
    output_buf = axidma_malloc(dev, output_size);
    if (output_buf == NULL) {
        rc = -ENOMEM;
        goto free_input_buf;
    }

    // Find the transmit, receive, and display channels
    tx_chans = axidma_get_dma_tx(dev, &num_tx);
    if (num_tx < 1) {
        fprintf(stderr, "Error: No transmit channels were found.\n");
        rc = -ENODEV;
        goto free_output_buf;
    }
    rx_chans = axidma_get_dma_rx(dev, &num_rx);
    if (num_rx < 1) {
        fprintf(stderr, "Error: No receive channels were found.\n");
        rc = -ENODEV;
        goto free_output_buf;
    }

    /* If the user didn't specify the channels, we assume that the transmit and
     * receive channels are the lowest numbered ones. */
    if (input_channel == -1 && output_channel == -1) {
        input_channel = tx_chans[0];
        output_channel = rx_chans[0];
    }

    // Perform the transfer
    rc = do_transfer(dev, input_channel, input_buf, input_size, output_channel,
            output_buf, output_size, tx_chans, num_tx, rx_chans, num_rx);
    if (rc < 0) {
        goto free_output_buf;
    }

    // Write the data to the output file
    printf("Writing output data to `%s`.\n", output_path);
    rc = robust_write(output_fd, output_buf, output_size);

free_output_buf:
    axidma_free(dev, output_buf, output_size);
free_input_buf:
    axidma_free(dev, input_buf, input_size);
ret:
    return rc;
}

/*----------------------------------------------------------------------------
 * Main
 *----------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
    int rc;
    char *input_path, *output_path;
    int input_channel, output_channel;
    int input_size, output_size;
    int input_fd, output_fd;
    axidma_dev_t axidma_dev;
    struct stat input_stat;

    if (parse_args(argc, argv, &input_path, &output_path, &input_channel,
                   &output_channel, &output_size) < 0) {
        rc = 1;
        goto ret;
    }

    // Try opening the input and output images
    input_fd = open(input_path, O_RDONLY);
    if (input_fd < 0) {
        perror("Error opening input file");
        rc = 1;
        goto ret;
    }
    output_fd = open(output_path, O_WRONLY|O_CREAT|O_TRUNC,
                     S_IWUSR|S_IRUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (output_fd < 0) {
        perror("Error opening output file");
        rc = -1;
        goto close_input;
    }

    // Initialize the AXIDMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL) {
        fprintf(stderr, "Error: Failed to initialize the AXI DMA device.\n");
        rc = 1;
        goto close_output;
    }

    // Get the size of the input file
    if (fstat(input_fd, &input_stat) < 0) {
        perror("Unable to get file statistics");
        rc = 1;
        goto destroy_axidma;
    }

    // If the output size was not specified by the user, set it to the default
    input_size = input_stat.st_size;
    if (output_size == -1) {
        output_size = input_size;
    }

    // Transfer the file over the AXI DMA
    rc = transfer_file(axidma_dev, input_fd, input_channel, input_size,
            output_fd, output_channel, output_size, output_path);
    rc = (rc < 0) ? 1 : 0;

destroy_axidma:
    axidma_destroy(axidma_dev);
close_output:
    assert(close(output_fd) == 0);
close_input:
    assert(close(input_fd) == 0);
ret:
    return rc;
}
