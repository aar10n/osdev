# Include at the bottom of external program makefiles to add build rules.
#
# Variables:
#   * NAME - The name of the external program
#   * GROUP - The group (e.g., bin, sbin)
#	* VERSION - The version of the external program
#     INSTALL_TARGETS - List of binaries to install (defaults to $(NAME))
#     SOURCE_DIR - Name of source directory under $(OBJ_DIR) (defaults to "source-$(VERSION)")
#     BUILD_DIR_NAME - Name of build directory under $(OBJ_DIR) (defaults to $(SOURCE_DIR))
#     CONFIGURE_DEPS - Additional dependencies for configure step
#     BUILD_DEPS - Additional dependencies for build step
#     INSTALL_EXTRA - Additional install commands
#
# Phony targets:
#   * download - Fetch and extract sources into $(SOURCE_PATH)
#   * build-main - Main build command
#     configure-extra - Perform configuration steps
#     install-extra - Additional install steps (called after default install)
#     clean-extra - Additional clean steps (called after default clean)
#
# * = must be defined by including makefile
##

ifeq ($(strip $(NAME)),)
$(error "NAME is not defined")
endif
ifeq ($(strip $(GROUP)),)
$(error "GROUP is not defined")
endif
ifeq ($(strip $(VERSION)),)
$(error "VERSION is not defined")
endif

include $(dir $(lastword $(MAKEFILE_LIST)))/../.config
include $(PROJECT_DIR)/scripts/defs.mk

# Default values
INSTALL_TARGETS ?= $(NAME)
SOURCE_DIR ?= $(NAME)-$(VERSION)
BUILD_DIR_NAME ?= $(SOURCE_DIR)

# Derived paths
EXTERN_DIR = $(GROUP)/$(NAME)
OBJ_DIR = $(BUILD_DIR)/$(EXTERN_DIR)
SOURCE_PATH = $(OBJ_DIR)/$(SOURCE_DIR)
BUILD_PATH = $(OBJ_DIR)/$(BUILD_DIR_NAME)

# Install paths
INSTALL_PREFIX ?= $(SYS_ROOT)
INSTALL_BINDIR = $(INSTALL_PREFIX)/$(GROUP)

# Mark main built target
BUILT_TARGET = $(OBJ_DIR)/.built-$(VERSION)
CONFIGURED_TARGET = $(OBJ_DIR)/.configured-$(VERSION)
DOWNLOADED_TARGET = $(OBJ_DIR)/.downloaded-$(VERSION)

.PHONY: all build install clean clean-all download

all: build

build: $(BUILT_TARGET)

$(OBJ_DIR):
	@mkdir -p $@

# Download and extract sources
$(DOWNLOADED_TARGET): | $(OBJ_DIR) download
	@touch $@

# Configure step (can be overridden by defining configure-extra)
$(CONFIGURED_TARGET): $(DOWNLOADED_TARGET) $(CONFIGURE_DEPS)
	$(MAKE) configure-internal
	@touch $@

.PHONY: configure-internal
configure-internal:
	@if $(MAKE) -n configure-extra >/dev/null 2>&1; then \
		$(MAKE) configure-extra; \
	fi

# Build step
$(BUILT_TARGET): $(CONFIGURED_TARGET) $(BUILD_DEPS)
	$(MAKE) build-internal
	@touch $@

.PHONY: build-internal
build-internal:
	$(MAKE) build-main

# Install step
install: $(BUILT_TARGET)
	@mkdir -p $(INSTALL_BINDIR)
	@$(foreach target,$(INSTALL_TARGETS),$(call install-target,$(target)))
	@if $(MAKE) -n install-extra >/dev/null 2>&1; then \
		$(MAKE) install-extra; \
	fi

# Clean step
clean:
	rm -rf $(OBJ_DIR)/.built-$(VERSION)
	rm -rf $(OBJ_DIR)/.configured-$(VERSION)
	rm -rf $(OBJ_DIR)/.downloaded-$(VERSION)
	@if $(MAKE) -n clean-extra >/dev/null 2>&1; then \
		$(MAKE) clean-extra; \
	fi

clean-all:
	rm -rf $(OBJ_DIR)


# Helper function to install a target
# Looks for the target in both BUILD_PATH and OBJ_DIR
define install-target
	if [ -f "$(OBJ_DIR)/$(1)" ]; then \
		echo "Installing $(1) from $(OBJ_DIR)"; \
		cp "$(OBJ_DIR)/$(1)" "$(INSTALL_BINDIR)/$(1)"; \
	elif [ -f "$(BUILD_PATH)/$(1)" ]; then \
		echo "Installing $(1) from $(BUILD_PATH)"; \
		cp "$(BUILD_PATH)/$(1)" "$(INSTALL_BINDIR)/$(1)"; \
	else \
		echo "Error: $(1) not found in $(BUILD_PATH) or $(OBJ_DIR)"; \
		exit 1; \
	fi
endef

