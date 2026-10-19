CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g -O0
OBJ = main.o lexer.o parser.o executor.o builtins.o

.PHONY: all clean test

all: msh

msh: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o msh

main.o: main.c lexer.h parser.h executor.h builtins.h
lexer.o: lexer.c lexer.h
parser.o: parser.c parser.h lexer.h
executor.o: executor.c executor.h parser.h lexer.h
builtins.o: builtins.c builtins.h parser.h lexer.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

lexer_test: lexer_test.c lexer.o lexer.h
	$(CC) $(CFLAGS) lexer_test.c lexer.o -o lexer_test

parser_test: parser_test.c parser.o lexer.o parser.h lexer.h
	$(CC) $(CFLAGS) parser_test.c parser.o lexer.o -o parser_test

test: lexer_test parser_test
	./lexer_test
	./parser_test

clean:
	rm -f msh $(OBJ) lexer_test parser_test
