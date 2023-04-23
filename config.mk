CC = clang
LD = clang
CFLAGS = -std=c17 -Wall -Wextra -g -march=native -O2 -pipe -flto
LDFLAGS = -flto -lm -lcheck

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
