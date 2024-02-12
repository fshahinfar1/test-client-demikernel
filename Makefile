# Copyright (c) Microsoft Corporation.
# Licensed under the MIT license.

#=======================================================================================================================
# Toolchain Configuration
#=======================================================================================================================

# C
INCDIR := ./include/
LIBS := ./libdemikernel.so
BINDIR := ./build
CC := gcc
CFLAGS := -Wall -Wextra -O3 -I $(INCDIR) -std=c99

#=======================================================================================================================
# Build Artifacts
#=======================================================================================================================

# C source files.
SRC_C := $(wildcard *.c)

# Object files.
OBJ := $(SRC_C:.c=.o)

# Suffix for executable files.
EXEC_SUFFIX := elf

# Compiles several object files into a binary.
COMPILE_CMD = $(CC) $(CFLAGS) $@.o common.o -o $(BINDIR)/$@.$(EXEC_SUFFIX) $(LIBS)

#=======================================================================================================================

# Builds everything.
all: common.o client

make-dirs:
	mkdir -p $(BINDIR)/

# Builds TCP ping pong test.
client: make-dirs common.o client.o
	$(COMPILE_CMD)

# Cleans up all build artifacts.
clean:
	@rm -rf $(OBJ)
	@rm -rf $(BINDIR)/client.$(EXEC_SUFFIX)

# Builds a C source file.
%.o: %.c
	$(CC) $(CFLAGS) $< -c -o $@
