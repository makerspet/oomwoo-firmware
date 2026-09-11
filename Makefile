CC ?= cc
CXX ?= c++
ARM_CC ?= arm-none-eabi-gcc
PIO ?= pio
BUILD_DIR ?= build

COMMON_WARNINGS := -Wall -Wextra -Werror -Wpedantic -Wconversion -Wsign-conversion
CFLAGS := -std=c11 $(COMMON_WARNINGS) -Iinclude
CXXFLAGS := -std=c++17 $(COMMON_WARNINGS) -Iinclude
SANITIZERS := -fsanitize=address,undefined -fno-omit-frame-pointer
ARM_TARGET_FLAGS ?= -mcpu=cortex-m4 -mthumb -ffreestanding

.PHONY: all test host-test cpp-test arm-check hil-build clean

all: test

test: host-test cpp-test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/test_cpu_watchdog: src/oomwoo_cpu_watchdog.c tests/test_cpu_watchdog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SANITIZERS) $^ -o $@

host-test: $(BUILD_DIR)/test_cpu_watchdog
	./$(BUILD_DIR)/test_cpu_watchdog

$(BUILD_DIR)/oomwoo_cpu_watchdog.o: src/oomwoo_cpu_watchdog.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SANITIZERS) -c $< -o $@

$(BUILD_DIR)/test_cpu_watchdog_cpp: tests/test_cpu_watchdog_cpp.cpp $(BUILD_DIR)/oomwoo_cpu_watchdog.o
	$(CXX) $(CXXFLAGS) $(SANITIZERS) $^ -o $@

cpp-test: $(BUILD_DIR)/test_cpu_watchdog_cpp
	./$(BUILD_DIR)/test_cpu_watchdog_cpp

arm-check: | $(BUILD_DIR)
	$(ARM_CC) $(CFLAGS) $(ARM_TARGET_FLAGS) -c src/oomwoo_cpu_watchdog.c -o $(BUILD_DIR)/oomwoo_cpu_watchdog_arm.o

hil-build:
	$(PIO) run -c platformio-watchdog-hil.ini -e nucleo_g474re_watchdog_hil

clean:
	rm -rf $(BUILD_DIR)
