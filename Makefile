CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g -O0
OBJ = main.o lexer.o parser.o executor.o builtins.o redirection.o terminal.o jobs.o job_control.o

.PHONY: all clean test

all: msh

msh: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o msh

main.o: main.c builtins.h executor.h jobs.h lexer.h parser.h terminal.h
lexer.o: lexer.c lexer.h
parser.o: parser.c parser.h lexer.h
executor.o: executor.c executor.h builtins.h job_control.h jobs.h redirection.h terminal.h parser.h lexer.h
builtins.o: builtins.c builtins.h redirection.h parser.h lexer.h
redirection.o: redirection.c redirection.h parser.h lexer.h
terminal.o: terminal.c terminal.h
jobs.o: jobs.c jobs.h
job_control.o: job_control.c job_control.h jobs.h terminal.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

lexer_test: lexer_test.c lexer.o lexer.h
	$(CC) $(CFLAGS) lexer_test.c lexer.o -o lexer_test

parser_test: parser_test.c parser.o lexer.o parser.h lexer.h
	$(CC) $(CFLAGS) parser_test.c parser.o lexer.o -o parser_test

jobs_test: jobs_test.c jobs.o jobs.h
	$(CC) $(CFLAGS) jobs_test.c jobs.o -o jobs_test

test: lexer_test parser_test jobs_test
	./lexer_test
	./parser_test
	./jobs_test

clean:
	rm -f msh $(OBJ) lexer_test parser_test jobs_test
