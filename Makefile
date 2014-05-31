CC ?= gcc
LIBS = -lxcb -lxcb-randr
CFLAGS += -std=c99 -pedantic -Wall -D_POSIX_C_SOURCE=200112L

SRC = rmon.c

rmon:
	$(CC) -o $@ $(SRC) $(CFLAGS) $(LIBS)

clean:
	rm -f rmon
