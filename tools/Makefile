# This makefile is for compiling host development tools
TOOLS =
ifeq ($(QEMU_BUILD_PLUGIN),y)
TOOLS += qemu-profile-plugin
endif

.DEFAULT_GOAL := all
all: $(TOOLS:%=all-%)
clean: $(TOOLS:%=clean-%)

all-%:
	$(MAKE) -C $* all

build-%:
	$(MAKE) -C $* build

clean-%:
	$(MAKE) -C $* clean
