.PHONY: all clean

CC      = gcc
CFLAGS  = -Wall -Wextra -Wno-implicit-fallthrough -std=gnu23 -fPIC -O2
LDFLAGS = -shared \
    -Wl,--wrap=malloc       -Wl,--wrap=calloc       -Wl,--wrap=realloc \
    -Wl,--wrap=reallocarray -Wl,--wrap=free          \
    -Wl,--wrap=strdup       -Wl,--wrap=strndup

all: librstack.so

librstack.so: rstack.o memory_tests.o
	$(CC) $(LDFLAGS) -o $@ $^

rstack.o: rstack.c rstack.h
	$(CC) $(CFLAGS) -c -o $@ $<

memory_tests.o: memory_tests.c memory_tests.h
	$(CC) $(CFLAGS) -c -o $@ $<

rstack_example: rstack_example.c librstack.so
	$(CC) -Wall -Wextra -std=gnu23 -o $@ $< -L. -lrstack -Wl,-rpath,.

clean:
	rm -f rstack.o memory_tests.o librstack.so rstack_example
