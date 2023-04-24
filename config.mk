warnings = -Wall -Wextra -Wshadow -Wpedantic -Wpointer-arith -Wcast-align \
           -Wstrict-prototypes -Wstrict-overflow=5 -Wwrite-strings -Wcast-qual \
           -Wunreachable-code

CC = clang
LD = clang
CFLAGS = -std=c17 $(warnings) -g -march=native -Wstrict-prototypes -O2 -pipe -flto
LDFLAGS = -flto -lm -lcheck

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
