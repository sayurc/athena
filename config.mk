CC = gcc
LD = gcc
CFLAGS = -std=c17 -Wall -Wextra -g -march=native -Ofast -pipe -flto
LDFLAGS = -flto -lm

PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
