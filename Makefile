CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -Iinclude -Isrc
LDFLAGS ?= -lm

SRCS = src/ufmod_load.c src/ufmod_play.c src/ufmod_mix.c src/ufmod_core.c
OBJS = $(SRCS:.c=.o)

all: libufmod.a ufmod_dump

libufmod.a: $(OBJS)
	ar rcs $@ $(OBJS)

ufmod_dump: tools/ufmod_dump.o libufmod.a
	$(CC) $(CFLAGS) -o $@ tools/ufmod_dump.o libufmod.a $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) tools/ufmod_dump.o libufmod.a ufmod_dump

.PHONY: all clean
