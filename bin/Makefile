# bin binaries
BIN_PROGS = \
	busybox

.DEFAULT_GOAL := all
all: $(BIN_PROGS:%=build-%)
install: $(BIN_PROGS:%=install-%)
clean: $(BIN_PROGS:%=clean-%)

build-%:
	$(MAKE) -C $* build

install-%:
	$(MAKE) -C $* install

clean-%:
	$(MAKE) -C $* clean
