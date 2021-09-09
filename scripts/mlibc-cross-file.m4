[binaries]
c = 'PREFIX/bin/x86_64-osdev-gcc'
cpp = 'PREFIX/bin/x86_64-osdev-g++'
ar = 'PREFIX/bin/x86_64-osdev-ar'
strip = 'PREFIX/bin/x86_64-osdev-strip'
pkgconfig = 'pkg-config'

[properties]
needs_exe_wrapper = true
pkg_config_libdir = 'PREFIX/usr/lib/pkgconfig'

[host_machine]
system = 'osdev'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
