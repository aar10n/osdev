comma := ,
space := $(subst ,, )

lowercase = $(shell echo '$1' | tr 'A-Z' 'a-z')
uppercase = $(shell echo '$1' | tr 'a-z' 'A-Z')

# Returns true if the given path exists, false otherwise.
# args:
#   $1 - path
exists = $(if $(realpath $(firstword $1)),true,false)

# Returns a list of <src> paths from a list of <src>:<dest> pairs.
# args:
#   $1 - list of <src>:<dest> pairs
# example:
#   $(call pairs-src-paths,foo:one bar:two baz:three) -> foo bar baz
pairs-src-paths = $(foreach dep,$1,$(firstword $(subst :, ,$(dep))))

# Converts a target name into a path relative to the project root.
# args:
#   $1: target name
# example:
#   $(call target-to-path,foo) -> foo
# 	$(call target-to-path,foo-bar) -> foo/bar
target2path = $(subst -,/,$1)

# Returns the source file for a given object file.
# args:
#   $1 - object file
object2source = $(patsubst %.o,$(PROJECT_DIR)/%,$(subst $(OBJ_DIR)/,,$1))

# Returns the object file for a given source file.
# args:
#   $1 - source file
source2object = $(patsubst %,$(OBJ_DIR)/%.o,$(subst $(PROJECT_DIR)/,,$1))

# Returns the given file with the provided prefix removed.
# args:
#   $1 - source file
#  	$2 - prefix (default = $(PROJECT_DIR))
normalize-file = $(if $2,$(patsubst $2/%,%,$1),$(patsubst $(PROJECT_DIR)/%,%,$1))

# Expands a list of variable names into a list of quoted value strings.
# args:
#   $1 - list of variable names
expand-vars = $(foreach var,$1,'$($(var))')

# Returns a list of override variables for a given file.
# args:
#   $1 - variable name
#   $2 - source file
# example:
#   $(call override-vars,CFLAGS,boot/foo.c) -> CFLAGS-boot-foo.c CFLAGS-boot CFLAGS
override-vars = $(shell perl $(PROJECT_DIR)/scripts/overrides.pl gen $1 $(call normalize-file,$2))

# ------------------ #
#  Target Functions  #
# ------------------ #

# Registers a target with a module.
# This should be called in each sub-directory's Makefile.
# args:
#   $1 - target name
#   $2 - module name
# example:
#   $(call register,foo,KERNEL)
register = $(eval $2_TARGETS += $1) $(eval $1-module = $2)

# Returns a list of fully qualified source files for a given target.
# The target must also be the name of a variable containing a list of
# source files relative to the target's directory.
# args:
#   $1: target name
# example:
#   foo = a.c b.c
#   $(call target-sources,foo) -> foo/a.c foo/b.c
target-sources = $($1:%=$(PROJECT_DIR)/$(call target2path,$1)/%)

# Returns a list of fully qualified object files for a given target.
# args:
#   $1: target name
# example:
#   $(call target-objects,foo) -> build/osdev/foo/a.c.o build/osdev/foo/b.c.o
target-objects = $($1:%=$(OBJ_DIR)/$(call target2path,$1)/%.o)

# Resolves the value of the specified target variable.
# args:
#   $1 - variable name
#   $2 - target name
# example:
#   CFLAGS = default
#   CFLAGS-foo = custom
#   $(call get-target-var,CFLAGS,foo) -> custom
#   $(call get-target-var,CFLAGS,bar) -> default
get-target-var = $(shell perl scripts/overrides.pl $(call expand-vars,$1-$2 $1))

# Returns the target name for a given file.
# args:
#   $1 - file
# example:
#   $(call target-name,kernel/foo.c) -> kernel
get-file-target = $(firstword $(subst /, ,$(call normalize-file,$(call object2source,$1))))


# ------------------ #
#  Module Functions  #
# ------------------ #

# Initializes a list of modules.
# This should be called after all targets have been registered.
# args:
#   $1 - list of module names
init-modules = $(foreach module,$1,$(eval $(module)_OBJECTS = $(call module-objects,$(module))))

# Returns a list of all source files for a given module.
# args:
#   $1 - module name
# example:
#   $(call module-sources,KERNEL) -> kernel/a.c fs/b.c
module-sources = $(foreach target,$($1_TARGETS),$(call target-sources,$(target)))

# Returns a list of all object files for a given module.
# args:
#   $1 - module name
# example:
#   $(call module-objects,KERNEL) -> build/osdev/kernel/a.c.o build/osdev/fs/b.c.o
module-objects = $(foreach target,$($1_TARGETS),$(call target-objects,$(target)))

# Resolves the value of the specified module variable.
# args:
#   $1 - variable name
#   $2 - module name
# example:
#   CFLAGS = default
#   KERNEL_CFLAGS = custom
#   $(call get-module-var,CFLAGS,KERNEL) -> custom
#   $(call get-module-var,CFLAGS,BOOT) -> default
get-module-var = $(shell perl scripts/overrides.pl $(call expand-vars,$2_$1 $1))

# Returns the module name for a given file.
# args:
#   $1 - file
# example:
#   $(call module-name,fs/foo.c) -> KERNEL
get-file-module = $($(call get-file-target,$1)-module)

# Resolves the value of the specified module variable for the given file.
# This is a convenience function that wraps get-module-var and accepts source
# or object file paths instead of a module name.
# args:
#   $1 - variable name
#   $2 - file
# example:
# 	VAR = 1
#   KERNEL_VAR = 2
#   $(call var,VAR,kernel/foo.c) -> 2
# 	$(call var,VAR,drivers/bar.c) -> 2
# 	$(call var,VAR,boot/baz.c) -> 1
var = $(call get-module-var,$1,$(call get-file-module,$(call object2source,$2)))



# if included in root makefile
ifeq ($(abspath $(firstword $(MAKEFILE_LIST))),$(PROJECT_DIR)/Makefile)
ifeq ($(call exists,Makefile.local),false)
$(shell cp toolchain/Makefile.template Makefile.local)
endif
endif
