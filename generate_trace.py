from scipy.io import mmread
import numpy as np
import os
import argparse


def load_matrix(mtx_path):
    print(f"[INFO] loading matrix from: {mtx_path}")
    A = mmread(mtx_path).tocsr()
    print("[INFO] load done")
    return A


def validate_matrix(A):
    rows, cols = A.shape
    nnz = A.nnz
    row_nnz = np.diff(A.indptr)

    stats = {
        "rows": rows,
        "cols": cols,
        "nnz": nnz,
        "min_row_nnz": int(row_nnz.min()) if len(row_nnz) > 0 else 0,
        "max_row_nnz": int(row_nnz.max()) if len(row_nnz) > 0 else 0,
        "avg_row_nnz": float(row_nnz.mean()) if len(row_nnz) > 0 else 0.0,
        "empty_rows": int(np.sum(row_nnz == 0)),
    }
    return stats


def print_stats(stats):
    print("\n[VALIDATE] matrix statistics")
    print(f"shape: ({stats['rows']}, {stats['cols']})")
    print(f"nonzeros: {stats['nnz']}")
    print(f"min row nnz: {stats['min_row_nnz']}")
    print(f"max row nnz: {stats['max_row_nnz']}")
    print(f"avg row nnz: {stats['avg_row_nnz']:.4f}")
    print(f"empty rows: {stats['empty_rows']}")


def generate_fiber_trace(A, out_path, include_write=True):
    """
    教学版 trace:
      READ_B_FIBER k
      WRITE_C_ROW i

    含义：
      对于 A 的第 i 行，若该行非零列号为 k1, k2, ...
      则 Gust 风格下会去访问 B 的对应 fibers: B[k1], B[k2], ...
    """
    total_reads = 0
    total_writes = 0

    with open(out_path, "w") as f:
        f.write("# Teaching/demo fiber trace for Gustavson-style sparse processing\n")
        f.write("# Format:\n")
        f.write("#   ROW <i>\n")
        f.write("#   READ_B_FIBER <k>\n")
        f.write("#   WRITE_C_ROW <i>\n\n")

        for i in range(A.shape[0]):
            start = A.indptr[i]
            end = A.indptr[i + 1]
            ks = A.indices[start:end]

            f.write(f"ROW {i}\n")
            for k in ks:
                f.write(f"READ_B_FIBER {int(k)}\n")
                total_reads += 1

            if include_write:
                f.write(f"WRITE_C_ROW {i}\n")
                total_writes += 1

    return total_reads, total_writes


def generate_address_trace(A, out_path, elem_bytes=8, include_write=True):
    """
    更像传统 cache trace 的版本：
    用虚拟基地址把 B fibers / C rows 映射成“伪地址”。

    注意：
    这只是学习和比较用，不是 SeaCache 官方精确地址格式。
    """
    # 给 B fibers 和 C rows 各分一个大的地址空间
    base_B = 0x10000000
    base_C = 0x20000000

    total_reads = 0
    total_writes = 0

    with open(out_path, "w") as f:
        f.write("# Teaching/demo address trace\n")
        f.write("# Format: R/W <hex_addr>\n\n")

        for i in range(A.shape[0]):
            start = A.indptr[i]
            end = A.indptr[i + 1]
            ks = A.indices[start:end]

            for k in ks:
                addr = base_B + int(k) * elem_bytes
                f.write(f"R {hex(addr)}\n")
                total_reads += 1

            if include_write:
                c_addr = base_C + int(i) * elem_bytes
                f.write(f"W {hex(c_addr)}\n")
                total_writes += 1

    return total_reads, total_writes


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mtx", required=True, help="path to input .mtx file")
    parser.add_argument("--out", required=True, help="output trace path")
    parser.add_argument(
        "--mode",
        choices=["fiber", "addr"],
        default="fiber",
        help="fiber: READ_B_FIBER / WRITE_C_ROW; addr: R/W hex_addr",
    )
    parser.add_argument(
        "--no-write",
        action="store_true",
        help="do not emit WRITE_C_ROW / W lines",
    )
    parser.add_argument(
        "--elem-bytes",
        type=int,
        default=8,
        help="element size in bytes for addr mode",
    )
    args = parser.parse_args()

    if not os.path.exists(args.mtx):
        raise FileNotFoundError(f"input mtx not found: {args.mtx}")

    A = load_matrix(args.mtx)

    stats = validate_matrix(A)
    print_stats(stats)

    include_write = not args.no_write

    print(f"\n[INFO] generating trace in mode = {args.mode}")
    if args.mode == "fiber":
        reads, writes = generate_fiber_trace(A, args.out, include_write=include_write)
    else:
        reads, writes = generate_address_trace(
            A, args.out, elem_bytes=args.elem_bytes, include_write=include_write
        )

    print(f"[INFO] trace saved to: {args.out}")
    print(f"[INFO] read events: {reads}")
    print(f"[INFO] write events: {writes}")
    print(f"[INFO] total events: {reads + writes}")

    print("\n[INFO] first 10 lines of trace:")
    with open(args.out, "r") as f:
        for idx, line in enumerate(f):
            print(line.rstrip())
            if idx >= 9:
                break


if __name__ == "__main__":
    main()
