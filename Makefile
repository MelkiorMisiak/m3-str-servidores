CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS = -pthread

.PHONY: all clean run
all: main

main: src/main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

run: main
	./main

clean:
	rm -f main
