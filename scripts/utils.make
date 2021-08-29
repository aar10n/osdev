#
# Common Helper Functions
#

# Returns the base directory of a file with any trailing
# slashes removed. If the base directory would be '.' it
# returns an empty string instead.
file_dir = $(strip $(filter-out .,$(patsubst %/,%,$(dir $1))))
# Returns the top-level directory of a file.
top_dir = $(strip $(firstword $(subst /, ,$(call file_dir,$1))))

# Returns the same thing as the `file_dir` function except
# it removes any build directory path prefix from the file
# first. If the file path does not contain the build directory
# the behavior is the same as the `file_dir` function.
normalized_file_dir = $(call file_dir,$(subst $(BUILD_DIR)/,,$1))
# Returns the normalized top-level directory of a file.
normalized_top_dir = $(call top_dir,$(subst $(BUILD_DIR)/,,$1))

# Returns a list of target object files from a list of sources.
# The first argument is the name of the variable containing the
# list of source files and the second argument is the is the build
# folder.
objects = $(patsubst %,$2/%.o,$($1))

debug-objects = $(patsubst %,$2/%.o.debug,$($1))

# Returns a list of source files from a given list of objects.
# This function takes only a single argument, which is the list
# of object files.
sources = $(patsubst %.o,%,$1)

# Applies the `objects` function to a list of targets. The targets
# should be the names of the targets (i.e the target variable name)
# rather than the sources for those targets.
target-objects = $(foreach t,$1,$(call objects,$t,$2))

source_file = $(patsubst %.o,%,$(subst $(BUILD_DIR)/,,$1))
all-paths = $(shell ./scripts/paths.sh $(dir $1))
to-prefixes = $(subst /,-,$(call all-paths,$1))

#
# Overriding Defaults
#

# --- Toolchains/Flags ---
# If a certain file or folder requires a different toolchain or build
# flags, the defaults can be easily overriden by defining variables in
# the form: `<DEFAULT>-<target>`.

_get_generic = $(if $(strip $($2-$1)),$($2-$1),$(if $(strip $3),$3,))
_get_flags_debug = $(if $(strip $($3-$2)),$3-$2 $3-$1,$3 $3-$1)
_get_flags = $(if $(strip $($3-$2)),$($3-$2) $($3-$1),$($3) $($3-$1))
_get_file = $(call normalized_top_dir,$1)/$(call sources,$(notdir $1))
_flags = $(strip $(subst \(\),,$(foreach prefix,$(call to-prefixes,$1),\($(call _get_generic,$(prefix),$2)\)) \($($2)\)))

# Returns the dependents from a list of first-dependents.
deps = $(call _get_generic,$(call file_dir,$(call _get_file,$(firstword $1))),DEPS,$1)

# Returns the specified program in the toolchain of a given file.
toolchain = $(firstword $(foreach prefix,$(call to-prefixes,$(call source_file,$1)) $2,$(call _get_generic,$(prefix),$2)) $($2))

# Returns the specified flags for a given file.
flags = $(subst \,,$(shell echo '$(call _flags,$(call source_file,$1),$2)' | sed -f scripts/first-group.sed))
