.POSIX:

include config.mk

OBJ = bit.o eval.o main.o move.o movegen.o pos.o rng.o search.o str.o threads.o\
      tt.o uci.o

all: $(OBJ)
	$(CC) $(LDFLAGS) -o athena $^ $(LDLIBS)

bit.o: bit.c bit.h
eval.o: eval.c eval.h
main.o: main.c
move.o: move.c move.h
movegen.o: movegen.c movegen.h
pos.o: pos.c pos.h
rng.o: rng.c rng.h
search.o: search.c search.h
str.o: str.c str.h
threads.o: threads.c threads.h
tt.o: tt.c tt.h
uci.o: uci.c uci.h

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f athena $(OBJ)

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f athena $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/athena
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f athena.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/athena.1

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/athena
	rm -f $(DESTDIR)$(MANPREFIX)/man1/athena.1

.PHONY: all clean install uninstall
