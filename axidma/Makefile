# AXI DMA Driver Makefile

# To use a different kernel directory, override the variable `KBUILD_DIR`

# If we're cross-compiling the module, use the default kernel source directory
# Otherwise, we're compiling for the same architecture, so use the system
# kernel directory.
ifdef CROSS_COMPILE
	KBUILD_DIR = $(SW_DIR)/kernels/xillinux/
else
	KERNEL_VERSION = $(shell uname -r)
	KBUILD_DIR = /lib/modules/$(KERNEL_VERSION)/build/
endif

# Add this module to the list of modules to compile
MODULE_NAME = axidma
$(MODULE_NAME)-objs := axi_dma.o axidma_dma.o axidma_chrdev.o
obj-m += $(MODULE_NAME).o

all:
	make -C $(KBUILD_DIR) SUBDIRS=$(PWD) modules

clean:
	make -C $(KBUILD_DIR) SUBDIRS=$(PWD) clean
