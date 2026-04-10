#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import json
import math
import argparse
import numpy as np
from scipy.sparse import coo_matrix, save_npz


def generate_countsketch(m, n, seed=0):
    """
    CountSketch matrix:
    - shape: (m, n)
    - exactly 1 nonzero per column
    - value is +1 or -1
    """
    rng = np.random.default_rng(seed)

    cols = np.arange(n)
    rows = rng.integers(low=0, high=m, size=n)
    vals = rng.choice([-1.0, 1.0], size=n)

    S = coo_matrix((vals, (rows, cols)), shape=(m, n), dtype=np.float32)
    return S


def generate_sparse_jl(m, n, s=2, seed=0, normalize=True):
    """
    Sparse JL matrix:
    - shape: (m, n)
    - s nonzeros per column
    - values are +1/sqrt(s) or -1/sqrt(s) if normalize=True
    """
    if s > m:
        raise ValueError("s cannot be larger than m")

    rng = np.random.default_rng(seed)

    rows = []
    cols = []
    vals = []

    scale = 1.0 / math.sqrt(s) if normalize else 1.0

    for j in range(n):
        chosen_rows = rng.choice(m, size=s, replace=False)
        chosen_vals = rng.choice([-1.0, 1.0], size=s) * scale

        for i, v in zip(chosen_rows, chosen_vals):
            rows.append(i)
            cols.append(j)
            vals.append(v)

    S = coo_matrix(
        (np.array(vals, dtype=np.float32), (np.array(rows), np.array(cols))),
        shape=(m, n),
        dtype=np.float32,
    )
    return S


def save_outputs(S, out_dir, name, matrix_type, m, n, s, seed):
    os.makedirs(out_dir, exist_ok=True)
    save_dir = os.path.join(out_dir, name)
    os.makedirs(save_dir, exist_ok=True)

    # COO
    triplets = np.column_stack((S.row, S.col, S.data))
    np.save(os.path.join(save_dir, "matrix_triplets.npy"), triplets)

    # CSR / CSC
    S_csr = S.tocsr()
    S_csc = S.tocsc()
    save_npz(os.path.join(save_dir, "matrix_csr.npz"), S_csr)
    save_npz(os.path.join(save_dir, "matrix_csc.npz"), S_csc)

    # metadata
    meta = {
        "name": name,
        "type": matrix_type,
        "shape": [m, n],
        "nnz": int(S.nnz),
        "density": float(S.nnz / (m * n)),
        "s": s,
        "seed": seed,
    }

    with open(os.path.join(save_dir, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    print(f"[OK] Saved to: {save_dir}")
    print(f"     shape = ({m}, {n}), nnz = {S.nnz}, density = {S.nnz / (m*n):.6e}")


def main():
    parser = argparse.ArgumentParser(description="Generate sparse sketching matrices")

    parser.add_argument("--type", type=str, required=True,
                        choices=["countsketch", "sparse_jl"],
                        help="Type of sketch matrix")

    parser.add_argument("--m", type=int, required=True,
                        help="Number of rows (sketch dimension)")
    parser.add_argument("--n", type=int, required=True,
                        help="Number of columns (original dimension)")
    parser.add_argument("--s", type=int, default=None,
                        help="Nonzeros per column (used for sparse_jl)")
    parser.add_argument("--seed", type=int, default=0,
                        help="Random seed")
    parser.add_argument("--out_dir", type=str, default="generated_sketch_matrices",
                        help="Output directory")
    parser.add_argument("--name", type=str, default=None,
                        help="Name of this generated matrix")

    args = parser.parse_args()

    if args.type == "countsketch":
        s = 1
        S = generate_countsketch(args.m, args.n, seed=args.seed)
    else:
        s = args.s if args.s is not None else 2
        S = generate_sparse_jl(args.m, args.n, s=s, seed=args.seed, normalize=True)

    if args.name is None:
        name = f"{args.type}_m{args.m}_n{args.n}_s{s}_seed{args.seed}"
    else:
        name = args.name

    save_outputs(S, args.out_dir, name, args.type, args.m, args.n, s, args.seed)


if __name__ == "__main__":
    main()