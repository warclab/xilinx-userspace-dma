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

# The list of example programs, and their corresponding targets
EXAMPLES = axidma_benchmark.c axidma_display_image.c axidma_transfer.c
TARGETS = $(EXAMPLES:.c=)

# Standard gcc flags for compilation
CFLAGS = -Wall -Wextra -Werror -std=gnu99
all examples $(TARGETS): CFLAGS += -O3
debug: CFLAGS += -g -O0

# Location of the header files
INC_DIRS = ../include
INC_FLAGS = $(addprefix -I , $(INC_DIRS))

# The location of the AXI DMA library used for compilation
LIB_DIR = ../libaxidma
LIBAXIDMA_FILES = libaxidma.c
LIBAXIDMA = $(addprefix $(LIB_DIR)/, $(LIBAXIDMA_FILES))

# The local helper function files used across the example programs.
UTIL = dma_util.c util.c

# Postpend a trailing slash to the output directory, if it is defined
ifdef OUTPUT_DIR
override OUTPUT_DIR := $(OUTPUT_DIR)/
endif

###############################################################################
# Macros
###############################################################################

# A macro to define a recipe for a given example program target
define CreateRecipe
ifdef OUTPUT_DIR
$(1): $(OUTPUT_DIR) $(1).c $$(LIBAXIDMA) $$(UTIL)
	$$(CC) $$(CFLAGS) $$(INC_FLAGS) $$(filter-out $$<,$$^) -o $$@
else
$(1): $(1).c $$(LIBAXIDMA) $$(UTIL)
	$$(CC) $$(CFLAGS) $$(INC_FLAGS) $$^ -o $$@
endif
endef

###############################################################################
# Targets
###############################################################################

# These targets don't correspond to actual generated files
.PHONY: all examples debug clean

# Build all of the example files
all examples debug: $(OUTPUT_DIR) $(TARGETS)

# Creates the output directory where the compiled files are stored
$(OUTPUT_DIR):
ifdef OUTPUT_DIR
	@mkdir -p $(OUTPUT_DIR)
endif

# Generate the rules for each example program
$(foreach target,$(TARGETS),$(eval $(call CreateRecipe, $(target))))

clean:
	rm -f $(TARGETS)
