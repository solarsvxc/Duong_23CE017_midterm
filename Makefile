CC = cc
CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
LDFLAGS =
LDLIBS =

OBJECTS = src/main.o src/options.o src/entry.o src/sort.o src/format.o

all: my_ls

my_ls: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/options.o: src/options.c include/options.h

src/entry.o: src/entry.c include/entry.h include/options.h

tests/test_options: tests/test_options.c src/options.o include/options.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_options.c src/options.o $(LDLIBS)

tests/test_entry: tests/test_entry.c src/entry.c src/entry.o src/options.o include/entry.h include/options.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_entry.c src/options.o $(LDLIBS)

test: tests/test_options tests/test_entry
	./tests/test_options
	./tests/test_entry

clean:
	rm -f my_ls $(OBJECTS) tests/test_options tests/test_entry

.PHONY: all test clean
