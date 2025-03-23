######################################
# Makefile                           #
# copyright (c) 2025, Nathan Gill    #
# https://github.com/OldUser101/tled #
######################################

CXX := gcc
CXXFLAGS := -g

BIN_DIR := bin
SRC_DIR := src

SRC_FILES := $(wildcard $(SRC_DIR)/*.c)
OBJ_FILES := $(patsubst $(SRC_DIR)/*.c, $(BIN_DIR)/*.o, $(SRC_FILES))
BIN_LEDS := $(BIN_DIR)/tled

all: $(BIN_LEDS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_LEDS): $(OBJ_FILES) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BIN_DIR)/%.o: $(SRC_DIR)/%.c | $(BIN_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BIN_DIR)

.PHONY:
	clean
