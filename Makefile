# Makefile for Q12 project
CC = gcc
CFLAGS = -O2 -pthread -DVALIDATE -g 
# Flags for performance builds without validation output
CFLAGS_BENCH = -O2 -pthread -g
BINDIR = build
SRC_MAIN        = main.c
SRC_LOCKFREE    = lockfree.c
SRC_LOCKFREERR  = lockfree_rr.c
SRC_LOCKFREERR_SIMD  = lockfree_rr_SIMD.c
SRC_UNOPT       = unoptimized.c

EXE_MAIN        = $(BINDIR)/main
EXE_LOCKFREE    = $(BINDIR)/lockfree
EXE_LOCKFREERR  = $(BINDIR)/lockfree_rr
EXE_LOCKFREERR_SIMD = $(BINDIR)/lockfree_rr_SIMD
EXE_UNOPT       = $(BINDIR)/unoptimized
# executables without validation
EXE_MAIN_BENCH       = $(BINDIR)/main_bench
EXE_LOCKFREE_BENCH   = $(BINDIR)/lockfree_bench
EXE_LOCKFREERR_BENCH = $(BINDIR)/lockfree_rr_bench
EXE_LOCKFREERR_SIMD_BENCH = $(BINDIR)/lockfree_rr_SIMD_bench
EXE_UNOPT_BENCH      = $(BINDIR)/unoptimized_bench

.PHONY: all all_bench main lockfree lockfree_rr lockfree_rr_SIMD unoptimized \
        main_bench lockfree_bench lockfree_rr_bench lockfree_rr_SIMD_bench unoptimized_bench \

all: main lockfree lockfree_rr lockfree_rr_SIMD unoptimized
all_bench: main_bench lockfree_bench lockfree_rr_bench lockfree_rr_SIMD_bench unoptimized_bench

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

lockfree_rr_SIMD:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS) -o $(EXE_LOCKFREERR_SIMD) $(SRC_LOCKFREERR_SIMD) -mavx2 -mfma

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

lockfree_rr_SIMD_bench:
	mkdir -p $(BINDIR)
	$(CC) $(CFLAGS_BENCH) -o $(EXE_LOCKFREERR_SIMD_BENCH) $(SRC_LOCKFREERR_SIMD) -mavx2 -mfma

# 使用 EXE=main 或 EXE=unoptimized 呼叫
validate:
	python3 evaluate.py $(EXE)

# ⇣⇣⇣ 這裡新增 lockfree_rr 測試 ⇣⇣⇣
throughput:
	@echo "threads       time_sec(lock-based)        time_sec(lock-free)         time_sec(lockfree-rr)        time_sec(lockfree-rr-SIMD)" > throughput.txt
	for i in $(shell seq 1 16); do \
		echo "Running with $$i threads..."; \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_MAIN_BENCH)        $(SRC_MAIN);        \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREE_BENCH)    $(SRC_LOCKFREE);    \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREERR_BENCH)  $(SRC_LOCKFREERR);  \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREERR_BENCH)  $(SRC_LOCKFREERR);  \
		$(CC) $(CFLAGS_BENCH) -DN_CORES=$$i -o $(EXE_LOCKFREERR_SIMD_BENCH)  $(SRC_LOCKFREERR_SIMD) -mavx2 -mfma;  \
		time_main=`          ./$(EXE_MAIN_BENCH)       2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		time_lockfree=`      ./$(EXE_LOCKFREE_BENCH)    2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		time_lockfree_rr=`   ./$(EXE_LOCKFREERR_BENCH)  2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		time_lockfree_rr_SIMD=`./$(EXE_LOCKFREERR_SIMD_BENCH) 2048 2048 2048 | grep Time | awk '{print $$2}'`; \
		printf "%-13d %-28s %-28s %-28s %-28s\n" $$i $$time_main $$time_lockfree $$time_lockfree_rr $$time_lockfree_rr_SIMD >> throughput.txt; \
	done

stealchunk:
	mkdir -p $(BINDIR)
	@echo "steal_chunk   time_sec" > throughput_stealchunk.txt
	for c in $(shell seq 1 16); do \
	echo "Testing STEAL_CHUNK=$$c"; \
	$(CC) $(CFLAGS_BENCH) -DSTEAL_CHUNK=$$c -o $(EXE_LOCKFREERR_SIMD_BENCH) $(SRC_LOCKFREERR_SIMD) -mavx2 -mfma; \
	time_chunk=`./$(EXE_LOCKFREERR_SIMD_BENCH) 2048 2048 2048 | grep Time | awk '{print $$2}'`; \
	printf "%-11d %-10s\n" $$c $$time_chunk >> throughput_stealchunk.txt; \
	done
	

PERF_OUT_DIR = perf_data
PERF_BIN ?= $(EXE_LOCKFREERR_SIMD_BENCH)
PERF_MAT ?= 2048 2048 2048

perf:
	mkdir -p $(PERF_OUT_DIR)
	@echo "🔍 Running perf on $(PERF_BIN) with matrix size $(PERF_MAT)"
	perf record -o $(PERF_OUT_DIR)/perf.data -- ./$(PERF_BIN) $(PERF_MAT)
	perf report -i $(PERF_OUT_DIR)/perf.data > $(PERF_OUT_DIR)/report.txt
	@echo "✅ perf report generated at $(PERF_OUT_DIR)/report.txt"
	@echo "💡 You can now run 'perf annotate -i $(PERF_OUT_DIR)/perf.data' for source-level hotspots."


plot:
	gnuplot gnuplot/plot_throughput.gp

plot_stealchunk:
	gnuplot gnuplot/plot_stealchunk.gp

clean:
	rm -rf $(BINDIR)


## Lock-based
#perf stat -r 10 -e cache-misses,cs ./build/main_bench 2048 2048 2048

# Lock-free
#perf stat -r 10 -e cache-misses,cs ./build/lockfree_bench 2048 2048 2048