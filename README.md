# Xilinx AXI DMA Driver and Library

# NOTE: This documentation is still a work in progress.

A Linux driver for Xilinx's AXI DMA IP block. The driver acts a generic layer between the processing system and the FPGA, and abstracts away the details of setting up DMA transactions. 

## Using the Driver and Library

### Setting up the Linux Kernel for the Driver

The kernel has to be built in a particular configuration before you can compile the driver with an out-of-tree build. This driver depends on the contiguous memory allocator (CMA), and the Xilinx DMA and VDMA drivers. 

Please make sure the following configuration options are enabled for you kernel (either look in the **.config** at the top-level of your kernel source tree, or run `make menuconfig`):
```bash
CONFIG_CMA=y
CONFIG_XILINX_DMAENGINES=y
CONFIG_XILINX_AXIDMA=y
CONFIG_XILINX_AXIVDMA=y
```

Depending on the exact kernel distribution you are using, then the configuration options for the Xilinx drivers may be slightly different (for example, the Analog Devices kernel is different). If the above doesn't work, then try these config options:
```bash
CONFIG_CMA=y
CONFIG_XILINX_DMAENGINES=y
CONFIG_XILINX_AXIDMA=y
CONFIG_XILINX_VDMA=y
```

Otherwise, you can search for the Xilinx DMA configuration options when you when `make menuconfig`, they should be clearly marked.

### Compiling the Driver

The Makefile supports out-of-tree buildling for the driver. To compile the driver, you need to specify the path to the top-level directory of the source tree of the kernel you're buildling it for.
```bash
make KBUILD_DIR=</path/to/kernel/tree> driver
```

If you're cross compiling, you can specify so via the usual methods. Specify `ARCH` and `CROSS_COMPILE` to the appropiate values. The Makefile supports both native and cross compilation. This will generate a kernel object file for the driver at `axidma/axidma.ko`. 

### Compiling the Examples

The driver and library comes with several example programs that show how to write a C application to use the software stack. One example is a performance evaluator for the DMA and whatever logic is on the PL. The other example is to display an image by sending it over DMA to some video IP, and the third example is a file transfer over DMA.

To compile the example programs:
```bash
make examples
```

If you're cross-compiling, make sure to specify the appropiate variables for cross-compilation. This will generate binary files under the `examples` directory that you can run.

### Using the Driver and Library in Your Application

The library is simply distributed as C source files, and it is not built into a static or shared library. Rather, when you write your application, you simply need to include the library's header file, `libaxidma/libaxidma.h`, and compile the library source with your application, `libaxidma/libaxidma.c`. The driver is compiled independently from your application.

### Updating the Kernel Command Line

In order to properly use the driver, you need to the the kernel to reserve a region of memory for contiguous memory allocations that the driver can use. This is done by specifying a `cma=<size>` option on the kernel command line. You can see to updating the kernel command to however best works for you. As an example, you could enter the following command from the U-Boot console when you boot up (assuming `bootargs` is already a defined variable):
```bash
setenv bootargs "${bootargs} cma=25M"
```

Naturally, depending on your application, you may need more or less than 25 MB of memory for DMA buffer allocations. Change this value to what works best for you.

### Running an Application

Compiling the driver will generate a kernel object file (.ko). Before you run your application, make sure the driver has been inserted into the kernel:
```bash
insmod axidma.ko
```

To verify that it has been successfully added to the kernel, run `dmesg` to check the kernel's log. You should see a message like the following:
```
axidma: axidma_dma.c: 776: DMA: Found 1 transmit channels and 1 receive channels.
```

Once the module is loaded into the kernel, you can then run your application. If anything goes wrong while running the application, be sure to check the kernel's log through `dmesg`; the driver will print out information any time there is an error.

## Running a Simple Loopback Example

To be completed.

## Additional Information on the Software Stack

### Purpose of the Driver and LIbrary

The driver mainly serves to act as the interface between userspace and Xilinx's AXI DMA driver in the kernel. The Xilinx driver does not have a direct interface to userspace, and it handles simply performing the writes and reads on MMIO registers for the AXI DMA IP. This driver handles translating this driver's functions into a clean interface. Also, the driver setting up the DMA buffers so that userspace can access them, and that they will allow for high-throughput transfers. 

The library, called libaxidma, serves to further abstract away the system call interface required to use the driver. The driver implements all of its functionality through IOCTL calls, so the library provides a stable interface, as the IOCTL interface may change over time. Additionally, the library provides an cleaner and more uniform interface to the driver.

## Features

1. Abstracts away the fine-grained deatils of the AXI DMA IP.
2. Transmit (ARM -> FPGA), receive (FPGA -> ARM), and two-way combined transfers.
3. Frame buffer mode (also called cyclic mode) that allows for the continuous transmission of N frame buffers.
3. Supports both synchronous and asynchronous modes for transfers.
4. Registration of a callback function that will be run upon completion of the transfer.
5. Memory allocator that allocates memory that is coherent between the processing system and the FPGA, by disabling caching for the allocated pages.
6. Memory allocator that allocates memory that is contiguous in physical memory, allowing for high-throughput DMA transfers.

## Limitations

1. Driver currently has no support for concurrency.
2. Only one process can access the DMA device at a time.

## Requirements

Our driver should be able to be used with any distribution of the 3.x or 4.x Linux kernels. It has been tested with our fork of the Xillinux kernel distribution. You can find the repository for that kernel [here](https://github.com/bperez77/zynq_linux). There is also a helpful wiki on how to build and run Linux for the Zedboard if you're new to the process.

## License (MIT)

The MIT License (MIT)

Copyright (c) 2016 Brandon Perez <bmperez@andrew.cmu.edu> <br>
Copyright (c) 2016 Jared Choi <jaewonch@andrew.cmu.edu>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
