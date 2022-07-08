# Toolchain

### Kernel Toolchain
```shell
# build binutils
bash toolchain/binutils.sh build x86_64 kernel
# build gcc
bash toolchain/gcc.sh build x86_64 kernel
```

### System Toolchain
```shell
# build binutils
bash toolchain/binutils.sh build x86_64 system
# build gcc + mlibc
bash toolchain/gcc.sh build x86_64 system
```
