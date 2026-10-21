CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -I../protocol/include
LDLIBS ?= -lcrypto

.PHONY: all clean

all: fortress-tool

fortress-tool: fortress-tool.c ../protocol/libfortress.a
	$(CC) $(CFLAGS) -o $@ fortress-tool.c ../protocol/libfortress.a $(LDLIBS)

../protocol/libfortress.a:
	$(MAKE) -C ../protocol libfortress.a

clean:
	rm -f fortress-tool
