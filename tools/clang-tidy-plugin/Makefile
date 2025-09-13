# Makefile for clang-tidy format string checker plugin
include ../../.config

PLUGIN_NAME = OsdevPlugin
PLUGIN_DIR = $(BUILD_DIR)/tools/clang-tidy-plugin
SHARED_LIB = $(PLUGIN_DIR)/$(PLUGIN_NAME).so

CXX = clang++
CXXFLAGS = -fPIC -Wall -Wextra
LLVM_CONFIG = llvm-config

LLVM_CXXFLAGS = $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS = $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS = $(shell $(LLVM_CONFIG) --libs)
LLVM_SYSTEM_LIBS = $(shell $(LLVM_CONFIG) --system-libs)
LLVM_INCLUDES = -I$(shell $(LLVM_CONFIG) --includedir)

# Platform-specific shared library flags
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	SHARED_FLAGS = -shared -Wl,-undefined,dynamic_lookup
else
	SHARED_FLAGS = -shared -fPIC
endif

SRCS := FmtStringCheck.cpp OsdevModule.cpp
OBJS := $(patsubst %.cpp,$(PLUGIN_DIR)/%.o,$(SRCS))


.PHONY: all clean test

all: $(SHARED_LIB)

clean:
	rm -rf $(PLUGIN_DIR)

test: $(SHARED_LIB)
	@echo "Testing plugin with sample file..."
	clang-tidy --load=$(SHARED_LIB) \
		-checks='-*,osdev-*' \
		test/sample.c -- \
		-I../../include

# Compilation rules

$(SHARED_LIB): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(SHARED_FLAGS) $(LLVM_LDFLAGS) -o $@ $^ $(LLVM_LIBS) $(LLVM_SYSTEM_LIBS)

$(PLUGIN_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LLVM_CXXFLAGS) $(LLVM_INCLUDES) -c -o $@ $<
