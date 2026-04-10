#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import json
import argparse
import numpy as np
from collections import deque


def save_trace(out_dir, name, addr_trace, meta):
    save_dir = os.path.join(out_dir, name)
    os.makedirs(save_dir, exist_ok=True)

    txt_path = os.path.join(save_dir, "address_trace.txt")
    npy_path = os.path.join(save_dir, "address_trace.npy")
    meta_path = os.path.join(save_dir, "metadata.json")
    stats_path = os.path.join(save_dir, "trace_stats.json")

    with open(txt_path, "w", encoding="utf-8") as f:
        for a in addr_trace:
            f.write(f"{int(a)}\n")

    np.save(npy_path, np.array(addr_trace, dtype=np.int64))

    stats = compute_trace_stats(np.array(addr_trace, dtype=np.int64))
    with open(stats_path, "w", encoding="utf-8") as f:
        json.dump(stats, f, indent=2)

    with open(meta_path, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)

    print(f"[OK] Saved: {save_dir}")
    print(f"     total addresses = {len(addr_trace)}")
    print(f"     unique deltas   = {stats['num_unique_deltas']}")
    print(f"     max jump        = {stats['max_positive_jump']}")


def compute_trace_stats(addr_trace, cacheline_bytes=64):
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
        pos = deltas[deltas > 0]
        stats["max_positive_jump"] = int(pos.max()) if len(pos) else 0
    else:
        stats["num_unique_deltas"] = 0
        stats["first_20_deltas"] = []
        stats["max_positive_jump"] = 0

    if len(addr_trace):
        stats["num_unique_cachelines"] = int(len(np.unique(addr_trace // cacheline_bytes)))
    else:
        stats["num_unique_cachelines"] = 0

    return stats


def generate_pointer_chasing_trace(
    num_nodes=4096,
    num_steps=20000,
    elem_size=8,
    base_addr=0,
    seed=0
):
    """
    Build a random linked structure:
      next[node] -> another node
    Then follow pointers for num_steps steps.

    Address emitted each step:
      base_addr + node_id * elem_size
    """
    rng = np.random.default_rng(seed)

    next_ptr = rng.permutation(num_nodes)
    current = int(rng.integers(0, num_nodes))

    trace = []
    for _ in range(num_steps):
        addr = base_addr + current * elem_size
        trace.append(addr)
        current = int(next_ptr[current])

    meta = {
        "workload": "pointer_chasing",
        "num_nodes": num_nodes,
        "num_steps": num_steps,
        "elem_size": elem_size,
        "base_addr": base_addr,
        "seed": seed,
    }
    return trace, meta


def generate_random_graph(num_nodes, avg_degree, seed=0):
    rng = np.random.default_rng(seed)
    graph = [[] for _ in range(num_nodes)]

    for u in range(num_nodes):
        deg = max(1, int(rng.poisson(avg_degree)))
        nbrs = rng.choice(num_nodes, size=min(deg, num_nodes), replace=False)
        graph[u].extend(int(v) for v in nbrs if v != u)

    return graph


def generate_bfs_trace(
    num_nodes=2048,
    avg_degree=4,
    max_visits=20000,
    elem_size=8,
    node_base=0,
    edge_base=1 << 24,
    seed=0
):
    """
    Generate a BFS-style trace.
    We simulate two memory regions:
      - node/visited array: node_base + u * elem_size
      - adjacency list entries: edge_base + edge_index * elem_size

    Every BFS step touches:
      1) current node metadata
      2) adjacency entries
      3) visited state / enqueue target nodes
    """
    graph = generate_random_graph(num_nodes, avg_degree, seed=seed)

    visited = [False] * num_nodes
    q = deque([0])
    visited[0] = True

    trace = []
    edge_counter = 0
    node_visits = 0

    while q and node_visits < max_visits:
        u = q.popleft()

        # access current node metadata
        trace.append(node_base + u * elem_size)
        node_visits += 1

        for v in graph[u]:
            # access adjacency entry
            trace.append(edge_base + edge_counter * elem_size)
            edge_counter += 1

            # access visited[v]
            trace.append(node_base + v * elem_size)

            if not visited[v]:
                visited[v] = True
                q.append(v)

                # enqueue / touch target node again
                trace.append(node_base + v * elem_size)

            if len(trace) >= max_visits:
                break

        if len(trace) >= max_visits:
            break

    meta = {
        "workload": "bfs",
        "num_nodes": num_nodes,
        "avg_degree": avg_degree,
        "max_visits": max_visits,
        "elem_size": elem_size,
        "node_base": node_base,
        "edge_base": edge_base,
        "seed": seed,
    }
    return trace, meta


def main():
    parser = argparse.ArgumentParser(description="Generate non-matmul benchmark address traces")
    parser.add_argument("--type", type=str, required=True, choices=["pointer", "bfs"])
    parser.add_argument("--name", type=str, required=True)
    parser.add_argument("--out_dir", type=str, default="generated_non_matmul_traces")
    parser.add_argument("--seed", type=int, default=0)

    # pointer chasing params
    parser.add_argument("--num_nodes", type=int, default=4096)
    parser.add_argument("--num_steps", type=int, default=20000)

    # bfs params
    parser.add_argument("--avg_degree", type=int, default=4)
    parser.add_argument("--max_visits", type=int, default=20000)

    args = parser.parse_args()

    if args.type == "pointer":
        trace, meta = generate_pointer_chasing_trace(
            num_nodes=args.num_nodes,
            num_steps=args.num_steps,
            seed=args.seed
        )
    else:
        trace, meta = generate_bfs_trace(
            num_nodes=args.num_nodes,
            avg_degree=args.avg_degree,
            max_visits=args.max_visits,
            seed=args.seed
        )

    meta["name"] = args.name
    save_trace(args.out_dir, args.name, trace, meta)


if __name__ == "__main__":
    main()