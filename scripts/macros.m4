changequote(`[', `]') dnl
divert(-1) dnl

dnl # ifempty(<value>, <if empty>, <ifnot empty>)
define([ifempty], [ifelse(len(patsubst([$1],[\s*])),0,$2,$3)])

dnl # isempty(<value>)
define([isempty], [ifelse(len(patsubst([$1],[\s*])),0,1,0)])

dnl # isequal(<value1>, <value2>)
define([isequal], [ifelse([$1],[$2],1,0)])

dnl # contains(<value>, <contains>)
define([contains], [eval(index([$1], [$2]) != -1)])

dnl # list_contains(<list>, <sep>, <contains>)
define([list_contains], [contains([translit([$1], [$2], [ ])], [$3])])

dnl
dnl
dnl

dnl # override(<variable>, <value>)
dnl # override(<variable>, <subtarget>, <value>)
define([override], [ifempty([$3],[$1-target = $2],[$1-target-$2 = $3])])

dnl # add_executable(<name>, <sources>)
define([add_executable], [dnl
  ifdef([target],
    [define([prefix], target[-]$1)],
    [define([target], []) define([prefix], $1)]
  )
# $1
prefix = patsubst([$2],[\s*])
prefix[]-y = $(call objects,prefix,$(BUILD_DIR)/target)
prefix[]-targets += $(BUILD)/apps/$1
target[]-targets += prefix
$(BUILD)/apps/$1: $(prefix[]-y)
	@mkdir -p $(@D)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ -o $[]@ dnl
divert(1)prefix divert[]dnl
])

dnl # add_library(<name>, <sources>, <headers>, [<static|dynamic>])
define([add_library], [dnl
  ifdef([target],
    [define([prefix], target[-]$1[])],
    [define([target], []) define([prefix], $1)]
  )
# $1
prefix = patsubst([$2],[\s*])
prefix[]-headers = $([patsubst] %, target/%, patsubst([$3],[\s*]))
prefix[]-y = $(call objects,prefix,$(BUILD_DIR)/target)
ifelse(list_contains([$4], [|], static), 1, dnl
prefix[]-targets += $(BUILD)/libs/$1.a
$(BUILD)/libs/$1.a: $(prefix[]-y)
	@mkdir -p $(@D)
	$(call toolchain,$<,AR) rcs $[]@ $^
)dnl
ifelse(list_contains([$4], [|], dynamic), 1, dnl
prefix[]-targets += $(BUILD)/libs/$1.so
$(BUILD)/libs/$1.so: $(prefix[]-y)
	@mkdir -p $(@D)
	$(call toolchain,$<,LD) $(call flags,$<,LDFLAGS) $^ -shared -o $[]@
)dnl
divert(1)prefix divert[]dnl
])

dnl # install_executable(<name>, <path>)
define([install_executable], [dnl
  ifdef([target],
    [define([prefix], target[-]$1[])],
    [define([target], []) define([prefix], $1)]
  )
  define([base], [ifempty([$2], [], [ifelse([$2], [/], [], $2)])])dnl
  define([bindir], [base/bin])
.PHONY: prefix[]-install
prefix[]-install: $(prefix[]-targets)
	@mkdir -p $(SYS_ROOT)[]bindir
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(foreach t,$^,$(t):bindir)dnl
divert(2)prefix divert[]dnl
])

dnl # install_library(<name>, <path>, [<subdir>])
define([install_library], [dnl
  ifdef([target],
    [define([prefix], target[-]$1[])],
    [define([target], []) define([prefix], $1)]
  )dnl
  define([base], [ifempty([$2], [], [ifelse([$2], [/], [], $2)])])dnl
  ifempty([$3],
    [define([libdir], [base/lib]) define([includedir], [base/include])],
    [define([libdir], [base/lib/$3]) define([includedir], [base/include/$3])]
  )
.PHONY: prefix[]-install
prefix[]-install: $(prefix[]-targets)
	@mkdir -p $(SYS_ROOT)[]libdir
	@mkdir -p $(SYS_ROOT)[]includedir
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(foreach t,$^,$(t):libdir)
	@scripts/copy-sysroot.sh $(SYS_ROOT) $(foreach t,$(prefix[]-headers),$(t):includedir)dnl
divert(2)prefix divert[]dnl
])

dnl # generate_targets()
define([generate_targets], [dnl
  ifdef([target],
    [define([prefix], target[-]$1[])],
    [define([target], []) define([prefix], $1)]
  )
# targets
target[]-targets = $(foreach t,undivert(1),$($(t)-targets))dnl
])

dnl # generate_install_targets()
define([generate_install_targets], [dnl
  ifdef([target],
    [define([prefix], target[-]$1[])],
    [define([target], []) define([prefix], $1)]
  )
# install targets
target[]-install-targets = $(foreach t,undivert(2),$(t)-install)dnl
])
divert(0) dnl
