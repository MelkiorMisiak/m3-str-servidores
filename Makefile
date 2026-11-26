CC = gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror
LDLIBS = -pthread

.PHONY: all clean run
all: m3

m3: src/main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDLIBS)

run: m3
	./m3

clean:
	rm -f m3
