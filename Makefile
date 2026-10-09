CC = cc
CPPFLAGS = -Iinclude
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror
LDFLAGS =
LDLIBS =

OBJECTS = src/main.o src/options.o src/entry.o src/sort.o src/format.o
TEST_PROGRAMS = tests/test_options tests/test_entry tests/test_sort tests/test_format

all: my_ls

my_ls: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

src/main.o: src/main.c include/entry.h include/format.h include/options.h include/sort.h

src/options.o: src/options.c include/options.h

src/entry.o: src/entry.c include/entry.h include/options.h

src/sort.o: src/sort.c include/sort.h include/entry.h include/options.h

src/format.o: src/format.c include/format.h include/entry.h include/options.h

tests/test_options: tests/test_options.c src/options.o include/options.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_options.c src/options.o $(LDLIBS)

tests/test_entry: tests/test_entry.c src/entry.c src/entry.o src/options.o include/entry.h include/options.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_entry.c src/options.o $(LDLIBS)

tests/test_sort: tests/test_sort.c src/sort.o include/sort.h include/entry.h include/options.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_sort.c src/sort.o $(LDLIBS)

tests/test_format: tests/test_format.c src/format.o src/options.o include/format.h include/entry.h include/options.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ tests/test_format.c src/format.o src/options.o $(LDLIBS)

unit-test: $(TEST_PROGRAMS)
	./tests/test_options
	./tests/test_entry
	./tests/test_sort
	./tests/test_format

test: unit-test my_ls
	MAKE="$(MAKE)" sh tests/test_ls.sh ./my_ls

clean:
	rm -f my_ls src/*.o tests/*.o $(TEST_PROGRAMS)

.PHONY: all test unit-test clean
