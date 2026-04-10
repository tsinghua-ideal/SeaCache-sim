#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import json
import argparse
import numpy as np
from scipy.sparse import load_npz


def load_sketch_matrix(sketch_path):
    """
    Load sparse sketch matrix from .npz
    """
    S = load_npz(sketch_path)
    return S.tocsc()   # easier to scan by column


def generate_dense_access_trace(
    S_csc,
    dense_rows,
    dense_cols,
    elem_size=4,
    base_addr=0,
    order="row-major"
):
    """
    Simulate accesses to dense matrix A during sketching operation Y = S @ A

    S: shape (m, n)
    A: shape (n, dense_cols)

    For each nonzero S[i, j], row j of A is accessed.
    We emit addresses for all elements in A[j, :].

    Returns:
      - addr_trace: list of byte addresses
      - access_meta: list of (sketch_col, dense_row)
    """
    m, n = S_csc.shape
    if dense_rows != n:
        raise ValueError(
            f"dense_rows must equal sketch matrix n. Got dense_rows={dense_rows}, n={n}"
        )

    addr_trace = []
    access_meta = []

    # Scan S column by column
    # Each column j corresponds to row j in dense matrix A
    for j in range(n):
        start = S_csc.indptr[j]
        end = S_csc.indptr[j + 1]

        # If this column has k nonzeros, row j of A is used k times
        nnz_in_col = end - start

        for _ in range(nnz_in_col):
            access_meta.append((j, j))   # (sketch_col, dense_row)

            for c in range(dense_cols):
                if order == "row-major":
                    addr = base_addr + ((j * dense_cols + c) * elem_size)
                elif order == "col-major":
                    addr = base_addr + ((c * dense_rows + j) * elem_size)
                else:
                    raise ValueError("order must be 'row-major' or 'col-major'")

                addr_trace.append(addr)

    return np.array(addr_trace, dtype=np.int64), access_meta


def save_trace(out_dir, name, addr_trace, access_meta, config):
    save_dir = os.path.join(out_dir, name)
    os.makedirs(save_dir, exist_ok=True)

    np.save(os.path.join(save_dir, "dense_access_trace.npy"), addr_trace)

    with open(os.path.join(save_dir, "dense_access_trace.txt"), "w", encoding="utf-8") as f:
        for a in addr_trace:
            f.write(f"{a}\n")

    with open(os.path.join(save_dir, "access_meta.txt"), "w", encoding="utf-8") as f:
        for sketch_col, dense_row in access_meta:
            f.write(f"sketch_col={sketch_col}, dense_row={dense_row}\n")

    with open(os.path.join(save_dir, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2)

    print(f"[OK] Saved trace to: {save_dir}")
    print(f"     total addresses = {len(addr_trace)}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate dense matrix access trace from a sparse sketch matrix"
    )

    parser.add_argument(
        "--sketch_path",
        type=str,
        required=True,
        help="Path to matrix_csc.npz or matrix_csr.npz from step 2"
    )
    parser.add_argument(
        "--dense_cols",
        type=int,
        required=True,
        help="Number of columns in dense matrix A"
    )
    parser.add_argument(
        "--elem_size",
        type=int,
        default=4,
        help="Bytes per element in dense matrix (default: 4)"
    )
    parser.add_argument(
        "--base_addr",
        type=int,
        default=0,
        help="Base byte address of dense matrix"
    )
    parser.add_argument(
        "--order",
        type=str,
        default="row-major",
        choices=["row-major", "col-major"],
        help="Dense matrix memory layout"
    )
    parser.add_argument(
        "--out_dir",
        type=str,
        default="generated_dense_access_traces",
        help="Output directory"
    )
    parser.add_argument(
        "--name",
        type=str,
        default="step3_test",
        help="Output folder name"
    )

    args = parser.parse_args()

    S = load_sketch_matrix(args.sketch_path)
    m, n = S.shape

    addr_trace, access_meta = generate_dense_access_trace(
        S_csc=S,
        dense_rows=n,
        dense_cols=args.dense_cols,
        elem_size=args.elem_size,
        base_addr=args.base_addr,
        order=args.order
    )

    config = {
        "name": args.name,
        "sketch_shape": [int(m), int(n)],
        "dense_shape": [int(n), int(args.dense_cols)],
        "elem_size": args.elem_size,
        "base_addr": args.base_addr,
        "layout": args.order,
        "num_trace_entries": int(len(addr_trace)),
        "sketch_path": args.sketch_path,
    }

    save_trace(args.out_dir, args.name, addr_trace, access_meta, config)


if __name__ == "__main__":
    main()