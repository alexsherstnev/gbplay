# GBPLAY
PROJECT = gbplay

# Derectories
BUILD_DIR = build
BIN_DIR = $(BUILD_DIR)/bin
OBJ_DIR = $(BUILD_DIR)/obj
SRC_DIR = src

# Toolchain
CC = gcc
RM = rm
MD = mkdir
CFLAGS = -Wall -Wextra -O0 -g -I$(SRC_DIR)
LDFLAGS = -lm -ldl

# Files
TARGET = $(BIN_DIR)/$(PROJECT)

SOURCES = \
	$(SRC_DIR)/gb/memory.c \
	$(SRC_DIR)/gb/interrupt.c \
	$(SRC_DIR)/gb/cpu.c \
	$(SRC_DIR)/gb/ppu.c \
	$(SRC_DIR)/gb/timer.c \
	$(SRC_DIR)/gb/gb.c \
	$(SRC_DIR)/main.c

INCLUDES = \
	$(SRC_DIR)/gb/defs.h \
	$(SRC_DIR)/gb/memory.h \
	$(SRC_DIR)/gb/interrupt.h \
	$(SRC_DIR)/gb/cpu.h \
	$(SRC_DIR)/gb/ppu.h \
	$(SRC_DIR)/gb/timer.h \
	$(SRC_DIR)/gb/gb.h

OBJ_NAMES = $(SOURCES:.c=.o)
OBJ = $(patsubst $(SRC_DIR)/%,$(OBJ_DIR)/%,$(OBJ_NAMES))

DEP_NAMES = $(SOURCES:.c=.d)
DEP = $(patsubst $(SRC_DIR)/%,$(OBJ_DIR)/%,$(DEP_NAMES))

# SDL3
CFLAGS += `pkg-config sdl3 --cflags`
LDFLAGS += `pkg-config sdl3 --libs --static`

# Compile
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@$(MD) -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Link
$(TARGET): $(OBJ) $(INCLUDES)
	@$(MD) -p $(dir $@)
	$(CC) $(LDFLAGS) $^ -o $@

# Include dependencies list
-include $(DEP)

# Phonies
.PHONY: all clean

all: $(TARGET)

clean:
	@$(RM) -rf $(BUILD_DIR)

