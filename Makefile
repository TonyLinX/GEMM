# Makefile for Q12 project
CC = gcc
CFLAGS = -O2 -pthread -DVALIDATE
BINDIR = build
SRC_MAIN = main.c
SRC_UNOPT = unoptimized.c

EXE_MAIN = $(BINDIR)/main
EXE_UNOPT = $(BINDIR)/unoptimized

.PHONY: all main unoptimized validate clean

all: main unoptimized

main:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_MAIN) $(SRC_MAIN)

unoptimized:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_UNOPT) $(SRC_UNOPT)

# 使用 EXE=main 或 EXE=unoptimized 呼叫
validate:
	python3 evaluate.py $(EXE)

clean:
	rm -rf $(BINDIR)
