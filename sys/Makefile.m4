include(`scripts/macros.m4') dnl
define([target], [sys]) dnl

#
# sys makefile
#

override(CC, $(SYS_ROOT)/bin/x86_64-osdev-gcc)
override(LD, $(SYS_ROOT)/bin/x86_64-osdev-gcc)

override(CFLAGS, -gdwarf -g3 -O0 -Wall)
override(LDFLAGS, -g3)
override(INCLUDE, -Iinclude/sys)

add_executable(console, console/main.c) dnl
install_executable(console, /)

add_executable(hello, hello/main.c) dnl
install_executable(hello, /usr)

add_library(libgui, libgui/ui.c, libgui/ui.h, static|dynamic) dnl
install_library(libgui, /usr, gui)

generate_targets()
generate_install_targets()
