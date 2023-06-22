# Include at the bottom of the program makefiles to add compilation rules.
#
#   * NAME
#   * GROUP
#   * SRCS
#     CFLAGS
#     INCLUDE
#     DEFINES
#     LDFLAGS
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

PROG_DIR = $(GROUP)/$(NAME)
OBJ_DIR = $(BUILD_DIR)/$(PROG_DIR)
OBJS = $(SRCS:%=$(OBJ_DIR)/%.o)

$(OBJ_DIR)/$(NAME): $(OBJS)
	@mkdir -p $(@D)
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

$(OBJ_DIR)/%.c.o: $(PROJECT_DIR)/$(PROG_DIR)/%.c
	@mkdir -p $(@D)
	@echo "CC $(CC)"
	$(CC) $(CFLAGS) $(INCLUDE) $(DEFINES) -c $< -o $@

.PHONY: build
build: $(OBJ_DIR)/$(NAME)

.PHONY: install
install: PREFIX = $(SYS_ROOT)
install: BINDIR = $(PREFIX)/$(GROUP)
install: build
	$(INSTALL) -d $(BINDIR)
	$(INSTALL) $(OBJ_DIR)/$(NAME) $(BINDIR)

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR)
