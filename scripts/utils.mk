# utility functions

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
#   $(call target-objects,foo) -> build/osdev/foo/a.c.o build/osdev/foo/b.c.o)
target-objects = $($1:%=$(OBJ_DIR)/$(call target2path,$1)/%.o)

# Returns a list of override variables for a given file.
# args:
#   $1 - variable name
#   $2 - source file
# example:
#   $(call override-vars,CFLAGS,boot/foo.c) -> CFLAGS-boot-foo.c CFLAGS-boot CFLAGS
override-vars = $(shell perl $(PROJECT_DIR)/scripts/overrides.pl gen $1 $(call normalize-file,$2))

# Resolves the value of the specified variable for the given source file.
# This function takes into account any overrides that exist as the path
# of the file is traversed up the directory tree.
# args:
#   $1 - variable name
#   $2 - source file
# example:
# 	VAR = 1
#   VAR-kernel = 2
#   VAR-kernel-foo.c = 3
#   $(call resolve,VAR,kernel/foo.c) -> 3
# 	$(call resolve,VAR,kernel/bar.c) -> 2
# 	$(call resolve,VAR,drivers/baz.c) -> 1
resolve = $(shell perl $(PROJECT_DIR)/scripts/overrides.pl $(call expand-vars,$(call override-vars,$1,$2)))
