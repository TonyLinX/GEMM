# Makefile for Q12 project
CC = gcc
CFLAGS = -O2 -pthread -DVALIDATE -g 
# Flags for performance builds without validation output
CFLAGS_BENCH = -O2 -pthread -g
BINDIR = build
SRC_MAIN        = main.c
SRC_LOCKFREE    = lockfree.c
SRC_LOCKFREERR  = lockfree_rr.c
SRC_LOCKFREERR_WS  = lockfree_rr_ws.c
SRC_UNOPT       = unoptimized.c

EXE_MAIN        = $(BINDIR)/main
EXE_LOCKFREE    = $(BINDIR)/lockfree
EXE_LOCKFREERR  = $(BINDIR)/lockfree_rr
EXE_LOCKFREERR_WS = $(BINDIR)/lockfree_rr_ws
EXE_UNOPT       = $(BINDIR)/unoptimized
# executables without validation
EXE_MAIN_BENCH       = $(BINDIR)/main_bench
EXE_LOCKFREE_BENCH   = $(BINDIR)/lockfree_bench
EXE_LOCKFREERR_BENCH = $(BINDIR)/lockfree_rr_bench
EXE_LOCKFREERR_WS_BENCH = $(BINDIR)/lockfree_rr_ws_bench
EXE_UNOPT_BENCH      = $(BINDIR)/unoptimized_bench

.PHONY: all all_bench main lockfree lockfree_rr lockfree_rr_ws unoptimized \
        main_bench lockfree_bench lockfree_rr_bench lockfree_rr_ws_bench unoptimized_bench \

all: main lockfree lockfree_rr lockfree_rr_ws unoptimized
all_bench: main_bench lockfree_bench lockfree_rr_bench lockfree_rr_ws_bench unoptimized_bench

main:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_MAIN) $(SRC_MAIN)

unoptimized:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_UNOPT) $(SRC_UNOPT)

lockfree:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_LOCKFREE) $(SRC_LOCKFREE)

lockfree_rr:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_LOCKFREERR) $(SRC_LOCKFREERR)

lockfree_rr_ws:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_LOCKFREERR_WS) $(SRC_LOCKFREERR_WS)

main_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_MAIN_BENCH) $(SRC_MAIN)

unoptimized_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_UNOPT_BENCH) $(SRC_UNOPT)

lockfree_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_LOCKFREE_BENCH) $(SRC_LOCKFREE)

lockfree_rr_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_LOCKFREERR_BENCH) $(SRC_LOCKFREERR)

lockfree_rr_ws_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_LOCKFREERR_WS_BENCH) $(SRC_LOCKFREERR_WS)

# 使用 EXE=main 或 EXE=unoptimized 呼叫
validate:
	python3 evaluate.py $(EXE)

# ⇣⇣⇣ 這裡新增 lockfree_rr 測試 ⇣⇣⇣
throughput:
	@echo "threads       time_sec(lock-based)        time_sec(lock-free)         time_sec(lockfree-rr)        time_sec(lockfree-rr-ws)" > throughput.txt
	for i in $(shell seq 1 16); do \
		echo "Running with $$i threads..."; \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_MAIN_BENCH)        $(SRC_MAIN);        \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREE_BENCH)    $(SRC_LOCKFREE);    \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREERR_BENCH)  $(SRC_LOCKFREERR);  \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREERR_BENCH)  $(SRC_LOCKFREERR);  \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREERR_WS_BENCH)  $(SRC_LOCKFREERR_WS);  \
		time_main=`          ./$(EXE_MAIN_BENCH)       2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		time_lockfree=`      ./$(EXE_LOCKFREE_BENCH)    2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		time_lockfree_rr=`   ./$(EXE_LOCKFREERR_BENCH)  2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		time_lockfree_rr_ws=`./$(EXE_LOCKFREERR_WS_BENCH) 2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		printf "%-13d %-28s %-28s %-28s %-28s\n" $$i $$time_main $$time_lockfree $$time_lockfree_rr $$time_lockfree_rr_ws >> throughput.txt; \
	done
	
plot:
	gnuplot gnuplot/plot_throughput.gp

clean:
	rm -rf $(BINDIR)


## Lock-based
#perf stat -r 10 -e cache-misses,cs ./build/main_bench 2048 2048 2048

# Lock-free
#perf stat -r 10 -e cache-misses,cs ./build/lockfree_bench 2048 2048 2048