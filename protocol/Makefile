CC ?= gcc
CFLAGS ?= -Wall -Wextra -Werror -Iinclude
LDFLAGS ?=
LDLIBS ?= -lcrypto

SRC := src/status.c src/crypto.c src/trust.c src/image.c src/soa.c
OBJ := $(SRC:.c=.o)

.PHONY: all test clean

all: libfortress.a test_policy

libfortress.a: $(OBJ)
	$(AR) rcs $@ $(OBJ)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

test_policy: tests/test_main.c libfortress.a
	$(CC) $(CFLAGS) -o $@ tests/test_main.c libfortress.a $(LDFLAGS) $(LDLIBS)

test: test_policy
	./test_policy

clean:
	rm -f $(OBJ) libfortress.a test_policy
