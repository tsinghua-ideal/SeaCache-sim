#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import json
import argparse
import numpy as np
from scipy.sparse import load_npz


def load_sketch_matrix(sketch_path):
    """
    Load sparse sketch matrix from .npz and convert to CSC format.
    We scan by column because each column j corresponds to dense row A[j, :].
    """
    S = load_npz(sketch_path)
    return S.tocsc()


def generate_dense_access_trace(
    S_csc,
    dense_cols,
    elem_size=4,
    base_addr=0,
    order="row-major",
    repeat_by_nnz=True,
):
    """
    Simulate dense matrix access trace for Y = S @ A

    S: shape (m, n)
    A: shape (n, dense_cols)

    For each column j in S:
      - if S[:, j] has k nonzeros,
      - then row A[j, :] participates k times in the sketching operation

    If repeat_by_nnz=True:
      emit row A[j, :] exactly k times
    else:
      emit row A[j, :] once if the column has any nonzero

    Returns:
      addr_trace: numpy array of byte addresses
      access_meta: list of dicts describing each row access
    """
    m, n = S_csc.shape
    dense_rows = n

    addr_trace = []
    access_meta = []

    for j in range(n):
        start = S_csc.indptr[j]
        end = S_csc.indptr[j + 1]
        row_indices_in_S = S_csc.indices[start:end]
        nnz_in_col = end - start

        if nnz_in_col == 0:
            continue

        repeat_count = nnz_in_col if repeat_by_nnz else 1

        for rep in range(repeat_count):
            access_meta.append({
                "sketch_col": int(j),
                "dense_row": int(j),
                "nnz_in_sketch_col": int(nnz_in_col),
                "repeat_id": int(rep),
                "sketch_rows_triggering_this": row_indices_in_S.tolist(),
            })

            for c in range(dense_cols):
                if order == "row-major":
                    addr = base_addr + ((j * dense_cols + c) * elem_size)
                elif order == "col-major":
                    addr = base_addr + ((c * dense_rows + j) * elem_size)
                else:
                    raise ValueError("order must be 'row-major' or 'col-major'")

                addr_trace.append(addr)

    return np.array(addr_trace, dtype=np.int64), access_meta


def compute_trace_stats(addr_trace, cacheline_bytes=64):
    """
    Basic statistics for the generated trace.
    """
    stats = {
        "num_addresses": int(len(addr_trace)),
        "min_addr": int(addr_trace.min()) if len(addr_trace) else None,
        "max_addr": int(addr_trace.max()) if len(addr_trace) else None,
        "num_unique_addresses": int(len(np.unique(addr_trace))) if len(addr_trace) else 0,
    }

    if len(addr_trace) >= 2:
        deltas = np.diff(addr_trace)
        stats["num_unique_deltas"] = int(len(np.unique(deltas)))
        stats["first_20_deltas"] = deltas[:20].tolist()

        positive_jumps = deltas[deltas > 0]
        stats["max_positive_jump"] = int(positive_jumps.max()) if len(positive_jumps) else 0
    else:
        stats["num_unique_deltas"] = 0
        stats["first_20_deltas"] = []
        stats["max_positive_jump"] = 0

    if len(addr_trace):
        cachelines = addr_trace // cacheline_bytes
        stats["num_unique_cachelines"] = int(len(np.unique(cachelines)))
    else:
        stats["num_unique_cachelines"] = 0

    return stats


def save_trace(out_dir, name, addr_trace, access_meta, config, stats):
    save_dir = os.path.join(out_dir, name)
    os.makedirs(save_dir, exist_ok=True)

    np.save(os.path.join(save_dir, "dense_access_trace.npy"), addr_trace)

    with open(os.path.join(save_dir, "dense_access_trace.txt"), "w", encoding="utf-8") as f:
        for a in addr_trace:
            f.write(f"{a}\n")

    with open(os.path.join(save_dir, "access_meta.txt"), "w", encoding="utf-8") as f:
        for item in access_meta:
            f.write(
                f"sketch_col={item['sketch_col']}, "
                f"dense_row={item['dense_row']}, "
                f"nnz_in_sketch_col={item['nnz_in_sketch_col']}, "
                f"repeat_id={item['repeat_id']}, "
                f"trigger_rows={item['sketch_rows_triggering_this']}\n"
            )

    with open(os.path.join(save_dir, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(config, f, indent=2)

    with open(os.path.join(save_dir, "trace_stats.json"), "w", encoding="utf-8") as f:
        json.dump(stats, f, indent=2)

    print(f"[OK] Saved trace to: {save_dir}")
    print(f"     total addresses = {len(addr_trace)}")
    print(f"     unique cachelines = {stats['num_unique_cachelines']}")
    print(f"     max positive jump = {stats['max_positive_jump']}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate realistic dense matrix access trace from sparse sketch matrix"
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
        help="Bytes per dense element (default: 4)"
    )
    parser.add_argument(
        "--base_addr",
        type=int,
        default=0,
        help="Base address of dense matrix"
    )
    parser.add_argument(
        "--order",
        type=str,
        default="row-major",
        choices=["row-major", "col-major"],
        help="Dense matrix layout"
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
        default="step3_fullscore",
        help="Output folder name"
    )
    parser.add_argument(
        "--no_repeat_by_nnz",
        action="store_true",
        help="If set, each dense row is emitted once per sketch column instead of once per nonzero"
    )

    args = parser.parse_args()

    S_csc = load_sketch_matrix(args.sketch_path)
    m, n = S_csc.shape

    addr_trace, access_meta = generate_dense_access_trace(
        S_csc=S_csc,
        dense_cols=args.dense_cols,
        elem_size=args.elem_size,
        base_addr=args.base_addr,
        order=args.order,
        repeat_by_nnz=not args.no_repeat_by_nnz,
    )

    stats = compute_trace_stats(addr_trace)

    config = {
        "name": args.name,
        "sketch_shape": [int(m), int(n)],
        "dense_shape": [int(n), int(args.dense_cols)],
        "elem_size": args.elem_size,
        "base_addr": args.base_addr,
        "layout": args.order,
        "repeat_by_nnz": not args.no_repeat_by_nnz,
        "num_trace_entries": int(len(addr_trace)),
        "sketch_path": args.sketch_path,
    }

    save_trace(args.out_dir, args.name, addr_trace, access_meta, config, stats)


if __name__ == "__main__":
    main()