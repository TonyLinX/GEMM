# Makefile for Q12 project
CC = gcc
CFLAGS = -O2 -pthread -DVALIDATE
# Flags for performance builds without validation output
CFLAGS_BENCH = -O2 -pthread
BINDIR = build
SRC_MAIN = main.c
SRC_LOCKFREE = lockfree.c
SRC_UNOPT = unoptimized.c

EXE_MAIN = $(BINDIR)/main
EXE_LOCKFREE = $(BINDIR)/lockfree
EXE_UNOPT = $(BINDIR)/unoptimized
# executables without validation
EXE_MAIN_BENCH = $(BINDIR)/main_bench
EXE_LOCKFREE_BENCH = $(BINDIR)/lockfree_bench
EXE_UNOPT_BENCH = $(BINDIR)/unoptimized_bench

.PHONY: all main lockfree unoptimized main_bench lockfree_bench unoptimized_bench validate clean

all: main lockfree unoptimized
all_bench: main_bench lockfree_bench unoptimized_bench

main:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_MAIN) $(SRC_MAIN)

unoptimized:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_UNOPT) $(SRC_UNOPT)

lockfree:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_LOCKFREE) $(SRC_LOCKFREE)

main_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_MAIN_BENCH) $(SRC_MAIN)

unoptimized_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_UNOPT_BENCH) $(SRC_UNOPT)

lockfree_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_LOCKFREE_BENCH) $(SRC_LOCKFREE)

# 使用 EXE=main 或 EXE=unoptimized 呼叫
validate:
	python3 evaluate.py $(EXE)

throughput:
	@echo "threads       time_sec(lock-based)        time_sec(lock-free)" > throughput.txt
	for i in $(shell seq 1 16); do \
		echo "Running with $$i threads..."; \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_MAIN_BENCH) $(SRC_MAIN); \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREE_BENCH) $(SRC_LOCKFREE); \
		time_main=`./build/main_bench 2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		time_lockfree=`./build/lockfree_bench 2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		printf "%-13d %-28s %-28s\n" $$i $$time_main $$time_lockfree >> throughput.txt; \
	done
plot:
	gnuplot gnuplot/plot_throughput.gp
clean:
	rm -rf $(BINDIR)