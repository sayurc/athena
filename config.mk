CC = clang
CFLAGS = -O2 -march=native -flto

LDFLAGS = -flto -fuse-ld=lld
LDLIBS = -lm

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
