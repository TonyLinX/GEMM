#!/usr/bin/env python3
import numpy as np
import subprocess
import sys

def run_exec(exe, m, n, p):
    """Run the executable with dimensions and capture its output."""
    cmd = [f"./{exe}", str(m), str(n), str(p)]
    cp = subprocess.run(cmd, capture_output=True, text=True, check=True)
    return cp.stdout

def parse_matrices(output):
    """Split the output at '---' and convert each block to a NumPy array."""
    blocks = []
    curr = []
    for line in output.splitlines():
        if line.strip() == '---':
            if curr:
                blocks.append(curr); curr = []
        elif line.strip():
            curr.append(line)
    if len(blocks) < 3:
        raise RuntimeError(f"Expected 3 matrices, found {len(blocks)} blocks")
    return [to_array(blocks[i]) for i in range(3)]

def to_array(lines):
    rows = []
    for row in lines:
        vals = [float(x) for x in row.split(',') if x.strip()]
        rows.append(vals)
    return np.array(rows, dtype=float)

def print_mat(mat, name):
    """Print a matrix in 2-decimals, comma-separated format."""
    r, c = mat.shape
    print(f"\nMatrix {name} ({r}×{c}):")
    for i in range(r):
        row = ", ".join(f"{mat[i,j]:.2f}" for j in range(c))
        print("  " + row)

def validate(A, B, C, tol=1e-5):
    D = A.dot(B)
    bad = np.argwhere(~np.isclose(D, C, atol=tol))
    if bad.size == 0:
        return True, None, None
    diffs = D - C
    info = [(i+1, j+1, D[i,j], C[i,j], diffs[i,j]) for i,j in bad]
    return False, info, D

def main():
    exe = "main"
    # exe = "unoptimized" 
    for size in range(2, 129):
        print(f"Testing {size}×{size}×{size}...", flush=True)
        out = run_exec(exe, size, size, size)
        A, B, C = parse_matrices(out)
        ok, mismatches, D = validate(A, B, C)
        if not ok:
            print(f"\n❌ Mismatch at size {size}:")
            # dump full matrices
            print_mat(A, "A")
            print_mat(B, "B")
            print_mat(C, "C (from C program)")
            print_mat(D, "D = A @ B (Python)")
            # then list the individual mismatches
            print("\nFirst few differing entries:")
            for r, c, comp, exp, diff in mismatches[:10]:
                print(f"  row {r}, col {c}: computed={comp}, expected={exp}, diff={diff}")
            sys.exit(1)
        else:
            print(" ✓ OK")
    print("\n✅ All sizes 2–128 validated successfully.")

if __name__ == "__main__":
    main()
