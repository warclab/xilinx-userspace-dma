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
TARGETS = $(EXAMPLES:%.c=%)

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

# If the output directory is defined, add a trailing slash
ifdef OUTPUT_DIR
override OUTPUT_DIR := $(OUTPUT_DIR)/
else
OUTPUT_DIR = ./
endif

# Update the targets with their output directory
TARGETS := $(addprefix $(OUTPUT_DIR),$(TARGETS))

###############################################################################
# Macros
###############################################################################

# A macro to define a recipe for a given example program target
define CreateRecipe
$(OUTPUT_DIR)$(1:%.c=%): $$(OUTPUT_DIR) $(1) $$(LIBAXIDMA) $$(UTIL)
	$$(CC) $$(CFLAGS) $$(INC_FLAGS) $$(filter-out $$<,$$^) -o $$@
endef

###############################################################################
# Targets
###############################################################################

# These targets don't correspond to actual generated files
.PHONY: all examples debug clean

# Build all of the example files
all examples debug: $(TARGETS)

# Creates the output directory where the compiled files are stored
$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

# Generate the rules for each example program
$(foreach example,$(EXAMPLES),$(eval $(call CreateRecipe,$(example))))

clean:
	rm -f $(TARGETS)
