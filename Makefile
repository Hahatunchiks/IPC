all: main.c ipc.c
	clang -std=c99 -Wall -pedantic *.c