include scripts/utils.make

CC = $(SYS_ROOT)/bin/x86_64-osdev-gcc
CXX = $(SYS_ROOT)/bin/x86_64-osdev-g++
LD = $(SYS_ROOT)/bin/x86_64-osdev-g++

CFLAGS = -gdwarf -g3 -O0 -Wall
CXXFLAGS = -gdwarf -g3 -O0 -Wall
LDFLAGS = -g3 -l:libfreetype.a -lpng -l:libharfbuzz.a

INCLUDE = -Isys/winserv/include -I$(SYS_ROOT)/usr/include/harfbuzz

winserv += main.cpp core/Buffer.cpp core/Drawable.cpp
winserv-y = $(call objects,winserv,$(BUILD_DIR)/sys/winserv/src)

.PHONY: all
all: $(BUILD)/apps/winserv

.PHONY: install
install: all
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(BUILD) $(BUILD)/apps/winserv:/bin

#

$(BUILD)/apps/winserv: $(winserv-y)
	$(LD) $(LDFLAGS) -o $@ $^

#

$(BUILD_DIR)/%.c.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) -o $@ -c $<

$(BUILD_DIR)/%.cpp.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(INCLUDE) -o $@ -c $<
