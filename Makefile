CC = gcc
CFLAGS = -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g -O0
OBJ = main.o expansion.o lexer.o parser.o executor.o builtins.o redirection.o terminal.o jobs.o job_control.o signals.o input.o

.PHONY: all clean test

all: msh

msh: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o msh

main.o: main.c builtins.h executor.h jobs.h lexer.h parser.h terminal.h signals.h input.h
main.o: expansion.h
expansion.o: expansion.c expansion.h lexer.h
lexer.o: lexer.c lexer.h
parser.o: parser.c parser.h lexer.h
executor.o: executor.c executor.h builtins.h job_control.h jobs.h redirection.h terminal.h parser.h lexer.h signals.h
builtins.o: builtins.c builtins.h redirection.h parser.h lexer.h jobs.h terminal.h job_control.h
redirection.o: redirection.c redirection.h parser.h lexer.h
terminal.o: terminal.c terminal.h
signals.o: signals.c signals.h terminal.h
input.o: input.c input.h jobs.h signals.h
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

expansion_test: expansion_test.c expansion.o lexer.o expansion.h lexer.h
	$(CC) $(CFLAGS) expansion_test.c expansion.o lexer.o -o expansion_test

test: msh lexer_test parser_test jobs_test expansion_test
	./lexer_test
	./parser_test
	./jobs_test
	./expansion_test
	python3 tests/integration_test.py

clean:
	rm -f msh $(OBJ) lexer_test parser_test jobs_test expansion_test
