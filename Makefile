name = TraySense
VERSION = 1.0.1
CC := gcc
CFLAGS := -O2
DEBUG_CFLAGS := -O0 -g
LDFLAGS :=
SRC_DIR = src/bin
BUILD_DIR = build
DBG_NAME = $(name)_debug

SRC := $(wildcard $(SRC_DIR)/*.c)
OBJ := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRC))

.PHONY: all default release debug run clean
.DEFAULT_GOAL := default

all: default

default: $(BUILD_DIR)/$(DBG_NAME)

release: $(BUILD_DIR)/$(name)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(name): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)
	chmod +x $@
	rm -f $(OBJ)

debug: $(BUILD_DIR)/$(DBG_NAME)
	gdb --args $< $(path)

$(BUILD_DIR)/$(DBG_NAME): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) $(DEBUG_CFLAGS) $(SRC) -o $@
	chmod +x $@

run: release
	@echo
	@echo
	@./$(BUILD_DIR)/$(name) $(path)

clean:
	rm -rf $(BUILD_DIR)/*
