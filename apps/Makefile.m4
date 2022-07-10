include(`scripts/macros.m4') dnl
define([target], [apps]) dnl

#
# sys makefile
#

override(CC, $(SYS_ROOT)/bin/x86_64-osdev-gcc)
override(CXX, $(SYS_ROOT)/bin/x86_64-osdev-g++)
override(LD, $(SYS_ROOT)/bin/x86_64-osdev-gcc)

override(CFLAGS, -gdwarf -g3 -O0 -Wall)
override(CXXFLAGS, -gdwarf -g3 -O0 -Wall)
override(LDFLAGS, -g3)
override(INCLUDE, -Iinclude/apps)

dnl link_library(console, libgui) dnl
link_library(console, libfreetype, ext|static) dnl
include_directory(console, /usr/include/freetype2) dnl
include_directory(console, /usr/include/harfbuzz) dnl
add_executable(console, console/main.c) dnl
install_executable(console, /)

add_executable(hello, hello/main.c) dnl
install_executable(hello, /usr)

generate_targets()
generate_install_targets()
