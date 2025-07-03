# GEMM

This project compares different approaches to matrix multiplication using a thread pool.

Executables built in `build/`:

- `main` – lock-based thread pool
- `lockfree` – single shared ring buffer
- `lockfree_rr` – lock-free pool with per-thread queues and round-robin dispatch
- `unoptimized` – single-threaded baseline

## Build and run

```bash
make all
./build/main 2048 2048 2048
```

Running `make` creates the `build/` directory.
