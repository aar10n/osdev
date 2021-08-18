#
# sys makefile
#


CC-sys = $(SYS_ROOT)/bin/x86_64-osdev-gcc
LD-sys = $(SYS_ROOT)/bin/x86_64-osdev-gcc

CFLAGS-sys = -gdwarf -g3 -O0 -Wall
LDFLAGS-sys = -g3
INCLUDE-sys = -Iinclude/sys

#test = $(patsubst %.o,%,$(subst $(BUILD_DIR)/,,$1))
# console
sys-console += console/main.c
sys-console-y = $(call objects,sys-console,$(BUILD_DIR)/sys)
$(BUILD)/apps/console: $(sys-console-y)
	@mkdir -p $(@D)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ -o $@

# hello
sys-hello += hello/main.c
sys-hello-y = $(call objects,sys-hello,$(BUILD_DIR)/sys)
$(BUILD)/apps/hello: $(sys-hello-y)
	@mkdir -p $(@D)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ -o $@


# sys-y
sys-programs = sys-console sys-hello
sys-targets = $(call programs,$(patsubst sys-%,%,$(sys-programs)))

