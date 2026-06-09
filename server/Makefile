# Makefile - serwer gry "Bombit" (czysty C + POSIX Threads)
#
# Uzycie:
#   make            # kompilacja serwera -> build/bombit_server
#   make run        # kompilacja i uruchomienie na domyslnym porcie 5000
#   make clean      # usuniecie plikow posrednich i binariow
#
# Uwaga (Windows): kod korzysta z gniazd POSIX i pthread, dlatego nalezy
# kompilowac w srodowisku Linux / WSL / MSYS2, a nie natywnym MSVC.

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11 -Iinclude -pthread
LDFLAGS := -pthread

SRC_DIR   := src
BUILD_DIR := build
TARGET    := $(BUILD_DIR)/bombit_server

SOURCES := $(wildcard $(SRC_DIR)/*.c)
OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)
	@echo "Zbudowano: $@"

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR)
	@echo "Wyczyszczono."
