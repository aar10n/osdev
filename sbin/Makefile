# system binaries
SBIN_PROGS = \
	getty \
	init \
	shell

.DEFAULT_GOAL := all
all: $(SBIN_PROGS:%=build-%)
install: $(SBIN_PROGS:%=install-%)
clean: $(SBIN_PROGS:%=clean-%)

build-%:
	$(MAKE) -C $* build

install-%:
	$(MAKE) -C $* install

clean-%:
	$(MAKE) -C $* clean
