# Include at the bottom of the program makefiles to add compilation rules.
#
# Variables:
#   * NAME
#   * GROUP
#   * SRCS
#     CFLAGS
#     INCLUDE
#     DEFINES
#	  LIBS
#
# * = must be defined by including makefile
##
ifeq ($(strip $(NAME)),)
$(error "NAME is not defined")
endif
ifeq ($(strip $(GROUP)),)
$(error "GROUP is not defined")
endif
ifeq ($(strip $(SRCS)),)
$(error "SRCS is not defined")
endif

include $(dir $(lastword $(MAKEFILE_LIST)))/../.config
include $(PROJECT_DIR)/scripts/defs.mk

INCLUDE += -I$(PROJECT_DIR)/include/uapi

PROG_DIR = $(GROUP)/$(NAME)
OBJ_DIR = $(BUILD_DIR)/$(PROG_DIR)
OBJS = $(SRCS:%=$(OBJ_DIR)/%.o)

$(OBJ_DIR)/$(NAME): $(OBJS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OBJ_DIR)/%.c.o: $(PROJECT_DIR)/$(PROG_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(INCLUDE) $(DEFINES) -c $< -o $@

.PHONY: build
build: $(OBJ_DIR)/$(NAME)

.PHONY: install
install: PREFIX = $(SYS_ROOT)
install: BINDIR = $(PREFIX)/$(subst .,/,$(GROUP))
install: build
	@mkdir -p $(BINDIR)
	cp $(OBJ_DIR)/$(NAME) $(BINDIR)

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR)
