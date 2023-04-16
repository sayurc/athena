CC = clang
LD = clang
CFLAGS = -std=c17 -Wall -Wextra -g -march=native -Ofast -pipe -flto
LDFLAGS = -flto -lm -lcheck

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
