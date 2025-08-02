# usr/bin binaries
USR_BIN_PROGS = doom keyboard

.DEFAULT_GOAL := all
all: $(USR_BIN_PROGS:%=build-%)
install: $(USR_BIN_PROGS:%=install-%)
clean: $(USR_BIN_PROGS:%=clean-%)

build-%:
	$(MAKE) -C $* build

install-%:
	$(MAKE) -C $* install

clean-%:
	$(MAKE) -C $* clean
