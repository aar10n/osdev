    
#
# sys makefile
#

CC-sys = $(SYS_ROOT)/bin/x86_64-osdev-gcc
LD-sys = $(SYS_ROOT)/bin/x86_64-osdev-gcc

CFLAGS-sys = -gdwarf -g3 -O0 -Wall
LDFLAGS-sys = -g3
INCLUDE-sys = -Iinclude/sys

          
  

sys-console-libs += -lfreetype
       
INCLUDE-sys-console += -I$(SYS_ROOT)/usr/include/freetype2
   
# console
sys-console = console/main.c
sys-console-y = $(call objects,sys-console,$(BUILD_DIR)/sys)
INCLUDE-sys-console += $(INCLUDE-sys)
sys-console-targets += $(BUILD)/apps/console
sys-targets += sys-console
$(BUILD)/apps/console: $(sys-console-y) | $(sys-console-deps)
	@mkdir -p $(@D)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ $(sys-console-libs) -o $@    
    
.PHONY: sys-console-install
sys-console-install: $(sys-console-targets)
	@mkdir -p $(SYS_ROOT)/bin
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(BUILD) $(foreach t,$^,$(t):/bin)

  
# hello
sys-hello = hello/main.c
sys-hello-y = $(call objects,sys-hello,$(BUILD_DIR)/sys)
INCLUDE-sys-hello += $(INCLUDE-sys)
sys-hello-targets += $(BUILD)/apps/hello
sys-targets += sys-hello
$(BUILD)/apps/hello: $(sys-hello-y) | $(sys-hello-deps)
	@mkdir -p $(@D)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ $(sys-hello-libs) -o $@    
    
.PHONY: sys-hello-install
sys-hello-install: $(sys-hello-targets)
	@mkdir -p $(SYS_ROOT)/usr/bin
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(BUILD) $(foreach t,$^,$(t):/usr/bin)

  
# libgui
sys-libgui = libgui/ui.c
sys-libgui-headers = $(patsubst %, sys/%, libgui/ui.h)
sys-libgui-y = $(call objects,sys-libgui,$(BUILD_DIR)/sys)
INCLUDE-sys-libgui += $(INCLUDE-sys)
sys-libgui-targets += $(BUILD)/libs/libgui.a
$(BUILD)/libs/libgui.a: $(sys-libgui-y)
	@mkdir -p $(@D)
	$(call toolchain,$<,AR) rcs $@ $^
sys-libgui-targets += $(BUILD)/libs/libgui.so
$(BUILD)/libs/libgui.so: $(sys-libgui-y)
	@mkdir -p $(@D)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ -shared -o $@
        
.PHONY: sys-libgui-install
sys-libgui-install: $(sys-libgui-targets)
	@mkdir -p $(SYS_ROOT)/usr/lib/
	@mkdir -p $(SYS_ROOT)/usr/include/
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(BUILD) $(foreach t,$^,$(t):/usr/lib/)
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(BUILD) $(foreach t,$(sys-libgui-headers),$(t):/usr/include/)

      
# targets
sys-targets = $(foreach t,sys-console sys-hello sys-libgui ,$($(t)-targets))
  
# install targets
sys-install-targets = $(foreach t,sys-console sys-hello sys-libgui ,$(t)-install)
