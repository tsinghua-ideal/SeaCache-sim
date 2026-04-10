#!/usr/bin/env python3
"""Data collection helper for SeaCache experiments.

Tasks:
1) Validate matrices/tile files exist.
2) Generate config JSON files for an experiment sweep.
3) Run scache for each (matrix, config) pair.
4) Parse simulator output text into a CSV table.
"""

from __future__ import annotations

import argparse
import csv
import itertools
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


METRIC_PATTERNS = {
    "total_cycle": re.compile(r"total cycle =\s*(\d+)"),
    "load_cycle": re.compile(r"load cycle =\s*(\d+)"),
    "multiply_cycle": re.compile(r"multiply cycle =\s*(\d+)"),
    "merge_writeback_cycle": re.compile(r"merge and writeback cycle =\s*(\d+)"),
    "total_sram_cycle": re.compile(r"total SRAM cycle =\s*(\d+)"),
    "total_dram_cycle": re.compile(r"total DRAM cycle =\s*(\d+)"),
    "total_pe_cycle": re.compile(r"total PE cycle =\s*(\d+)"),
    "total_dram_access_a": re.compile(r"total DRAM access A =\s*(\d+)"),
    "total_dram_access_b": re.compile(r"total DRAM access B =\s*(\d+)"),
    "total_dram_access_c": re.compile(r"total DRAM access C =\s*(\d+)"),
    "hitrate": re.compile(r"hitrate =\s*([0-9eE+\-.]+)"),
}


@dataclass(frozen=True)
class ExperimentConfig:
    transpose: int
    cachesize: float
    memorybandwidth: float
    pecnt: int
    srambank: int
    baselinetest: int
    condensedop: bool

    def json_obj(self, tile_dir: str, output_dir: str) -> Dict[str, object]:
        return {
            "transpose": self.transpose,
            "cachesize": self.cachesize,
            "memorybandwidth": self.memorybandwidth,
            "PEcnt": self.pecnt,
            "srambank": self.srambank,
            "baselinetest": self.baselinetest,
            "condensedOP": self.condensedop,
            "tileDir": tile_dir,
            "outputDir": output_dir,
        }

    def tag(self) -> str:
        condensed = 1 if self.condensedop else 0
        return (
            f"t{self.transpose}_c{self.cachesize:g}_bw{self.memorybandwidth:g}"
            f"_pe{self.pecnt}_sb{self.srambank}_b{self.baselinetest}_co{condensed}"
        )


def parse_csv_numbers(raw: str, conv):
    return [conv(x.strip()) for x in raw.split(",") if x.strip()]


def discover_matrices(tile_dir: Path) -> List[str]:
    return sorted([p.name for p in tile_dir.iterdir() if p.is_file() and not p.name.startswith(".")])


def matrix_file_path(matrix: str, roots: Sequence[Path]) -> Optional[Path]:
    for root in roots:
        if root.name == "largedata":
            candidate = root / matrix / f"{matrix}.mtx"
        else:
            candidate = root / f"{matrix}.mtx"
        if candidate.exists():
            return candidate
    return None


def validate_inputs(
    matrices: Sequence[str],
    tile_dir: Path,
    matrix_roots: Sequence[Path],
    scache_path: Path,
) -> Tuple[List[str], List[Tuple[str, str]]]:
    problems: List[Tuple[str, str]] = []

    if not scache_path.exists():
        problems.append(("scache", f"binary missing at {scache_path}"))
    elif not scache_path.is_file():
        problems.append(("scache", f"not a file: {scache_path}"))

    valid_matrices: List[str] = []
    for matrix in matrices:
        tile_file = tile_dir / matrix
        matrix_file = matrix_file_path(matrix, matrix_roots)
        missing = []
        if not tile_file.exists():
            missing.append(f"tile file missing: {tile_file}")
        if matrix_file is None:
            looked = ", ".join(str(p) for p in matrix_roots)
            missing.append(f"matrix .mtx missing in roots: {looked}")

        if missing:
            problems.append((matrix, "; ".join(missing)))
        else:
            valid_matrices.append(matrix)

    return valid_matrices, problems


def build_experiment_grid(args: argparse.Namespace) -> List[ExperimentConfig]:
    if args.profile == "quick":
        return [
            ExperimentConfig(0, 1.0, 34.0, 16, 16, 0, False),
            ExperimentConfig(0, 2.0, 68.0, 32, 32, 0, False),
            ExperimentConfig(0, 4.0, 136.0, 64, 32, 0, False),
        ]

    if args.profile == "balanced":
        return [
            ExperimentConfig(0, 1.0, 68.0, 32, 32, 0, False),
            ExperimentConfig(0, 2.0, 68.0, 32, 32, 0, False),
            ExperimentConfig(0, 4.0, 68.0, 32, 32, 0, False),
            ExperimentConfig(0, 2.0, 34.0, 32, 32, 0, False),
            ExperimentConfig(0, 2.0, 136.0, 32, 32, 0, False),
            ExperimentConfig(0, 2.0, 68.0, 16, 32, 0, False),
            ExperimentConfig(0, 2.0, 68.0, 64, 32, 0, False),
            ExperimentConfig(0, 2.0, 68.0, 32, 16, 0, False),
            ExperimentConfig(0, 2.0, 68.0, 32, 64, 0, False),
        ]

    if args.profile == "full":
        configs = [
            ExperimentConfig(t, c, bw, pe, sb, b, co)
            for t, c, bw, pe, sb, b, co in itertools.product(
                parse_csv_numbers(args.transpose_values, int),
                parse_csv_numbers(args.cachesize_values, float),
                parse_csv_numbers(args.bandwidth_values, float),
                parse_csv_numbers(args.pecnt_values, int),
                parse_csv_numbers(args.srambank_values, int),
                parse_csv_numbers(args.baseline_values, int),
                [False if x == 0 else True for x in parse_csv_numbers(args.condensed_values, int)],
            )
        ]
        return configs

    raise ValueError(f"Unknown profile: {args.profile}")


def write_config_files(
    configs: Sequence[ExperimentConfig],
    config_dir: Path,
    tile_dir_for_json: str,
    output_dir_for_json: str,
) -> Dict[ExperimentConfig, Path]:
    config_dir.mkdir(parents=True, exist_ok=True)
    mapping: Dict[ExperimentConfig, Path] = {}
    for cfg in configs:
        path = config_dir / f"{cfg.tag()}.json"
        with path.open("w", encoding="utf-8") as f:
            json.dump(cfg.json_obj(tile_dir_for_json, output_dir_for_json), f, indent=2)
            f.write("\n")
        mapping[cfg] = path
    return mapping


def expected_output_filename(matrix: str, cfg: ExperimentConfig) -> str:
    prefix = "Base_" if cfg.baselinetest else "SeaCache_"
    return (
        f"CGust{prefix}{cfg.cachesize:.6f}MB_{cfg.memorybandwidth:.6f}GBs_"
        f"{cfg.pecnt}PEs_{cfg.srambank}sbanks__{matrix}_{matrix}_RR_{cfg.transpose}.txt"
    )


def extract_metrics(output_text: str) -> Dict[str, object]:
    metrics: Dict[str, object] = {}
    for key, pattern in METRIC_PATTERNS.items():
        match = pattern.search(output_text)
        if not match:
            metrics[key] = None
            continue
        value = match.group(1)
        if key == "hitrate":
            metrics[key] = float(value)
        else:
            metrics[key] = int(value)
    return metrics


def run_one(
    scache_path: Path,
    matrix: str,
    cfg: ExperimentConfig,
    cfg_path: Path,
    output_dir: Path,
    timeout_s: int,
    dry_run: bool,
) -> Tuple[str, int, Optional[Path], str]:
    cmd = [str(scache_path), matrix, matrix, str(cfg_path)]

    if dry_run:
        return ("dry_run", 0, None, " ".join(cmd))

    output_dir.mkdir(parents=True, exist_ok=True)
    before_mtime = {p: p.stat().st_mtime for p in output_dir.glob("*.txt") if p.is_file()}

    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout_s)

    expected = output_dir / expected_output_filename(matrix, cfg)
    if expected.exists():
        return ("ok" if proc.returncode == 0 else "failed", proc.returncode, expected, " ".join(cmd))

    after = [p for p in output_dir.glob("*.txt") if p.is_file()]
    changed = []
    for p in after:
        old = before_mtime.get(p)
        new = p.stat().st_mtime
        if old is None or new > old:
            changed.append((new, p))
    changed.sort(key=lambda x: x[0], reverse=True)
    candidate = changed[0][1] if changed else None

    return ("ok" if proc.returncode == 0 else "failed", proc.returncode, candidate, " ".join(cmd))


def write_csv(rows: List[Dict[str, object]], csv_path: Path) -> None:
    csv_path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        csv_path.write_text("", encoding="utf-8")
        return

    fields = list(rows[0].keys())
    with csv_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def parse_args(argv: Sequence[str]) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="SeaCache data collection script.")
    p.add_argument("--repo-root", default=".", help="SeaCache repo root")
    p.add_argument("--scache", default="./scache", help="Path to scache binary")
    p.add_argument("--tile-dir", default="./tiles", help="Directory of tile files")
    p.add_argument("--output-dir", default="./output", help="Directory where scache writes .txt results")
    p.add_argument("--config-dir", default="./generated_configs", help="Directory to write generated config JSON")
    p.add_argument("--results-csv", default="./output/collected_results.csv", help="CSV file for extracted metrics")
    p.add_argument("--matrices", default="", help="Comma-separated matrix names (default: discover from tiles)")
    p.add_argument("--max-matrices", type=int, default=0, help="Limit number of matrices (0 means no limit)")
    p.add_argument("--profile", choices=["quick", "balanced", "full"], default="balanced")
    p.add_argument("--timeout", type=int, default=3600, help="Timeout per run in seconds")
    p.add_argument("--dry-run", action="store_true", help="Only validate + generate configs, skip simulator runs")

    # Used only when --profile full
    p.add_argument("--transpose-values", default="0")
    p.add_argument("--cachesize-values", default="1,2,4")
    p.add_argument("--bandwidth-values", default="34,68,136")
    p.add_argument("--pecnt-values", default="16,32,64")
    p.add_argument("--srambank-values", default="16,32,64")
    p.add_argument("--baseline-values", default="0")
    p.add_argument("--condensed-values", default="0")

    return p.parse_args(argv)


def main(argv: Sequence[str]) -> int:
    args = parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    scache_path = (repo_root / args.scache).resolve()
    tile_dir = (repo_root / args.tile_dir).resolve()
    output_dir = (repo_root / args.output_dir).resolve()
    config_dir = (repo_root / args.config_dir).resolve()
    results_csv = (repo_root / args.results_csv).resolve()

    matrix_roots = [
        repo_root / "data",
        repo_root / "largedata",
        repo_root / "dense",
        repo_root / "bfs",
    ]

    if args.matrices.strip():
        matrices = [m.strip() for m in args.matrices.split(",") if m.strip()]
    else:
        matrices = discover_matrices(tile_dir)

    if args.max_matrices > 0:
        matrices = matrices[: args.max_matrices]

    valid_matrices, problems = validate_inputs(matrices, tile_dir, matrix_roots, scache_path)

    print(f"[{datetime.now().isoformat(timespec='seconds')}] Validation summary")
    print(f"  requested matrices: {len(matrices)}")
    print(f"  valid matrices:     {len(valid_matrices)}")
    print(f"  validation issues:  {len(problems)}")
    for item, problem in problems:
        print(f"  - {item}: {problem}")

    if not valid_matrices:
        print("No valid matrices found. Exiting.")
        return 1

    configs = build_experiment_grid(args)
    cfg_map = write_config_files(
        configs,
        config_dir,
        tile_dir_for_json=args.tile_dir if args.tile_dir.endswith("/") else args.tile_dir + "/",
        output_dir_for_json=args.output_dir if args.output_dir.endswith("/") else args.output_dir + "/",
    )

    print(f"[{datetime.now().isoformat(timespec='seconds')}] Generated {len(configs)} configs in {config_dir}")

    rows: List[Dict[str, object]] = []
    total_jobs = len(valid_matrices) * len(configs)
    done_jobs = 0

    for matrix in valid_matrices:
        mtx_path = matrix_file_path(matrix, matrix_roots)
        assert mtx_path is not None

        for cfg in configs:
            done_jobs += 1
            cfg_path = cfg_map[cfg]
            print(
                f"[{done_jobs}/{total_jobs}] matrix={matrix} cfg={cfg.tag()} -> running"
                if not args.dry_run
                else f"[{done_jobs}/{total_jobs}] matrix={matrix} cfg={cfg.tag()} -> dry run"
            )

            status, returncode, out_path, cmd = run_one(
                scache_path=scache_path,
                matrix=matrix,
                cfg=cfg,
                cfg_path=cfg_path,
                output_dir=output_dir,
                timeout_s=args.timeout,
                dry_run=args.dry_run,
            )

            row: Dict[str, object] = {
                "matrix": matrix,
                "matrix_file": str(mtx_path),
                "config_file": str(cfg_path),
                "status": status,
                "returncode": returncode,
                "command": cmd,
                "output_file": str(out_path) if out_path else "",
                "transpose": cfg.transpose,
                "cachesize": cfg.cachesize,
                "memorybandwidth": cfg.memorybandwidth,
                "PEcnt": cfg.pecnt,
                "srambank": cfg.srambank,
                "baselinetest": cfg.baselinetest,
                "condensedOP": int(cfg.condensedop),
            }

            if out_path and out_path.exists() and not args.dry_run:
                text = out_path.read_text(encoding="utf-8", errors="ignore")
                row.update(extract_metrics(text))
            else:
                for key in METRIC_PATTERNS:
                    row[key] = None

            rows.append(row)

    write_csv(rows, results_csv)
    print(f"[{datetime.now().isoformat(timespec='seconds')}] Wrote {len(rows)} rows to {results_csv}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main(sys.argv[1:]))
    except subprocess.TimeoutExpired as exc:
        print(f"Timeout while running command: {exc.cmd}", file=sys.stderr)
        raise SystemExit(2)
