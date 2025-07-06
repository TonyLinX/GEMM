# GEMM

This project compares different approaches to matrix multiplication using a thread pool.

Executables built in `build/`:

- `main` – lock-based thread pool
- `lockfree` – single shared ring buffer
- `lockfree_rr` – lock-free pool with per-thread queues and round-robin dispatch
- `lockfree_rr_SIMD` – lock-free pool with per-thread queues, round-robin dispatch and work stealing
- `unoptimized` – single-threaded baseline

## Build and run

```bash
make all
./build/<executable> 2048 2048 2048
```

Replace `<executable>` with one of the programs above (e.g. `lockfree_rr_SIMD`).
Running `make` creates the `build/` directory.
