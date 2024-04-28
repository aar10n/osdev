# fmt

CFLAGS := -std=gnu17 -pedantic -Wall -Wextra
LDFLAGS := 

LIB_SRCS := fmt.c fmtlib.c
LIB_OBJS := $(LIB_SRCS:.c=.o)
LIB_CFLAGS := $(CFLAGS) -Ofast -ffreestanding -fPIC -fno-omit-frame-pointer -Wno-gnu-statement-expression
LIB_LDFLAGS := $(LDFLAGS) -nostdlib -nostdinc

TEST_SRCS := test.c
TEST_OBJS := $(TEST_SRCS:.c=.o)
TEST_CFLAGS := $(CFLAGS)
TEST_LDFLAGS := $(LDFLAGS)

.PHONY: all clean

all: lib test

lib: $(LIB_OBJS)
	$(AR) rcs libfmt.a $(LIB_OBJS)

test: $(TEST_OBJS) lib
	$(CC) $(TEST_LDFLAGS) $(TEST_OBJS) -o test -L. -lfmt

clean:
	rm -f $(LIB_OBJS)
	rm -f $(TEST_OBJS)
	rm -f libfmt.a
	rm -f test


test/%.o: test/%.c
	$(CC) $(TEST_CFLAGS) -I. -c $< -o $@

%.o: %.c
	$(CC) $(LIB_CFLAGS) -I. -c $< -o $@

