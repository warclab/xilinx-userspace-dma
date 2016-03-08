# Makefile
#
# Date: March 6, 2016
# Author: Brandon Perez
# Author: Jared Choi

###############################################################################
# Configuration
###############################################################################

# Allow the user to specify cross-compilation from the command line
CC = $(CROSS_COMPILE)gcc

# Standard gcc flags for compilation
CFLAGS = -Wall -Wextra -Werror -std=gnu99
all examples $(TARGETS): CFLAGS += -O3
debug: CFLAGS += -g -O0

# Location of the header files
INC_DIRS = ../inc ../axidma
INC_FLAGS = $(addprefix -I , $(INC_DIRS))

# The location of the AXI DMA library used for compilation
LIB_DIR = ../lib
LIBAXIDMA_FILES = libaxidma.c
LIBAXIDMA = $(addprefix $(LIB_DIR)/, $(LIBAXIDMA_FILES))

# The helper library files used for compilation
UTIL_FILES = dma_util.c util.c
UTIL = $(addprefix $(LIB_DIR)/, $(UTIL_FILES))

# The list of example programs, and their corresponding targets
EXAMPLES = axidma_benchmark.c axidma_display_image.c axidma_transfer.c
TARGETS = $(EXAMPLES:.c=)

###############################################################################
# Targets
###############################################################################

# A macro to define a recipe for a given example program target
define CreateRecipe
$(1): $(1).c $(LIBAXIDMA) $(UTIL)
	$(CC) $(CFLAGS) $(INC_FLAGS) $$^ -o $$@
endef

# None of the targets correspond to actual source files
.PHONY: all examples debug $(TARGETS) clean

# Build all of the example files
all examples debug: $(TARGETS)

# Generate the rules for each example program
$(foreach target, $(TARGETS), $(eval $(call CreateRecipe, $(target))))

clean:
	rm -f $(EXAMPLE_FILES)
