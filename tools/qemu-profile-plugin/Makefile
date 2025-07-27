# Makefile for QEMU Flame Graph Plugin
include ../../.config

ifndef QEMU_INCLUDE_DIR
$(error "QEMU_INCLUDE_DIR is not defined. Please set it in your .config file.")
endif

PLUGIN_NAME = qemu-profile
PLUGIN_DIR = $(BUILD_DIR)/tools/qemu-profile-plugin
PLUGIN = $(PLUGIN_DIR)/$(PLUGIN_NAME).so

CC ?= gcc
CFLAGS = -Wall -Wextra -O2 -g -fPIC -I$(QEMU_INCLUDE_DIR)
CFLAGS += $(shell pkg-config --cflags glib-2.0)
LDFLAGS = $(shell pkg-config --libs glib-2.0)

# macOS needs special flags for plugins
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -undefined dynamic_lookup
endif

# Source files
SRCS = qemu-profile.c

# Output files
PLUGIN_BASIC = $(PLUGIN_NAME_BASIC).so
PLUGIN_ADVANCED = $(PLUGIN_NAME_ADVANCED).so

# Symbol resolver (kept as separate file)
RESOLVER = resolve_symbols.py

# Default target
.NOTPARALLEL:
all: check-deps build

.PHONY: check-deps
check-deps: $(PLUGIN_DIR)/.check_deps

.PHONY: build
build: $(PLUGIN)

.PHONY: clean
clean:
	@rm -rf $(PLUGIN_DIR)
	@mkdir -p $(PLUGIN_DIR)

# Internal rules

$(PLUGIN_DIR)/.check_deps:
	@mkdir -p $(PLUGIN_DIR)
	@echo "Checking dependencies..."
	@which $(CC) > /dev/null || (echo "Error: gcc not found" && exit 1)
	@pkg-config --exists glib-2.0 || (echo "Error: glib-2.0 not found" && exit 1)
	@test -f $(QEMU_INCLUDE_DIR)/qemu-plugin.h || (echo "Error: qemu-plugin.h not found in $(QEMU_INCLUDE_DIR)" && exit 1)
	@echo "All dependencies found"
	@touch $@

$(PLUGIN): $(SRCS)
	$(CC) $(CFLAGS) $(QEMU_CFLAGS) -shared -o $@ $< $(LDFLAGS)
	@echo "built $@"

# Dependency tracking
-include $(patsubst %.c,$(PLUGIN_DIR)/%.d,$(SRCS))

$(PLUGIN_DIR)/%.d: %.c
	@$(CC) -MM $(CFLAGS) $< > $@

