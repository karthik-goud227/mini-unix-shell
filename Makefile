CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g -O0

.PHONY: all clean

all: msh

msh: main.o
	$(CC) $(CFLAGS) main.o -o msh

main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o main.o

clean:
	rm -f msh main.o
