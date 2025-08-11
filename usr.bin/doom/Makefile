NAME = doom
GROUP = usr.bin
VERSION = master

ALWAYS_REBUILD = y
include ../../scripts/extern.mk

SRCS = main.c
CFLAGS += -g -fPIE
LDFLAGS += -pie
INCLUDE += -I$(SOURCE_PATH)

SRCS_DOOM = dummy.c am_map.c doomdef.c doomstat.c dstrings.c d_event.c d_items.c d_iwad.c d_loop.c \
		    d_main.c d_mode.c d_net.c f_finale.c f_wipe.c g_game.c hu_lib.c hu_stuff.c info.c i_cdmus.c \
		    i_endoom.c i_joystick.c i_scale.c i_sound.c i_system.c i_timer.c memio.c m_argv.c m_bbox.c \
		    m_cheat.c m_config.c m_controls.c m_fixed.c m_menu.c m_misc.c m_random.c p_ceilng.c p_doors.c \
		    p_enemy.c p_floor.c p_inter.c p_lights.c p_map.c p_maputl.c p_mobj.c p_plats.c p_pspr.c p_saveg.c \
		    p_setup.c p_sight.c p_spec.c p_switch.c p_telept.c p_tick.c p_user.c r_bsp.c r_data.c r_draw.c r_main.c \
		    r_plane.c r_segs.c r_sky.c r_things.c sha1.c sounds.c statdump.c st_lib.c st_stuff.c s_sound.c tables.c \
		    v_video.c wi_stuff.c w_checksum.c w_file.c w_main.c w_wad.c z_zone.c w_file_stdc.c w_file_posix.c \
		    i_input.c i_video.c

SRC_DOOM_PATHS = $(SRCS_DOOM:%.c=$(SOURCE_PATH)/%.c)
OBJS = $(SRCS:%.c=$(OBJ_DIR)/%.c.o) $(SRC_DOOM_PATHS:$(SOURCE_PATH)/%.c=$(SOURCE_PATH)/%.c.o)

DOOMGENERIC_URL = https://github.com/aar10n/doomgeneric/archive/refs/heads/$(VERSION).tar.gz
DOOMGENERIC_ARCHIVE = doomgeneric-$(VERSION).tar.gz

.DEFAULT_GOAL := install

# Required: Download and extract sources
.PHONY: download
download:
	mkdir -p $(OBJ_DIR)
	@if [ ! -f "$(OBJ_DIR)/$(DOOMGENERIC_ARCHIVE)" ]; then \
		echo "Downloading doomgeneric..."; \
		cd $(OBJ_DIR) && wget -O $(DOOMGENERIC_ARCHIVE) $(DOOMGENERIC_URL); \
	fi
	@if [ ! -d "$(SOURCE_PATH)" ]; then \
		echo "Extracting doomgeneric..."; \
		cd $(OBJ_DIR) && tar -xf $(DOOMGENERIC_ARCHIVE); \
		mv $(OBJ_DIR)/doomgeneric-$(VERSION)/doomgeneric $(SOURCE_PATH); \
		rm -rf $(OBJ_DIR)/doomgeneric-$(VERSION); \
	fi

# Required: Main build command
.PHONY: build-main
build-main: $(OBJ_DIR)/$(NAME)

# Optional: Clean doomgeneric
.PHONY: clean-extra
clean-extra:
	@echo "Cleaning doomgeneric build..."
	rm -f $(SOURCE_PATH)/*.c.o

# Internal targets

$(OBJ_DIR)/$(NAME): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OBJ_DIR)/%.c.o: $(PROJECT_DIR)/$(EXTERN_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) $(DEFINES) -c $< -o $@

$(SOURCE_PATH)/%.c.o: $(SOURCE_PATH)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) $(DEFINES) -c $< -o $@
