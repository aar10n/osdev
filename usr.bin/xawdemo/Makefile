NAME = xawdemo
GROUP = usr.bin
SRCS = main.c

CFLAGS += -g -fPIE
LDFLAGS += -pie -L$(SYS_ROOT)/usr/lib
INCLUDE += -I$(SYS_ROOT)/usr/include
LIBS += -lXaw7 -lXmu -lXt -lSM -lICE -lXext -lXpm -lX11 -lxcb -lXau -lpthread

include ../../scripts/prog.mk
