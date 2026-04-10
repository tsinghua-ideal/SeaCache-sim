import json
import re
import subprocess
import csv
from pathlib import Path
from datetime import datetime

REPO_ROOT = Path(__file__).resolve().parent

EXE = REPO_ROOT / "build" / "scache"

# ⭐关键：传“矩阵名”，不要带 data/ 和 .mtx
A = "filter3D"
B = "filter3D"

BASE_CONFIG = REPO_ROOT / "config" / "config.json"

SWEEP_ROOT = REPO_ROOT / "output" / "sweep_runs"
SWEEP_ROOT.mkdir(parents=True, exist_ok=True)
CSV_PATH = SWEEP_ROOT / "results.csv"

CACHE_SIZES = [1, 2, 4, 8, 16]


def grab(pattern, text):
    m = re.search(pattern, text)
    return m.group(1) if m else ""


def run_once(config_dict, run_name):
    run_dir = SWEEP_ROOT / run_name
    run_dir.mkdir(exist_ok=True)

    run_output_dir = run_dir / "program_output"
    run_output_dir.mkdir(exist_ok=True)

    # 这俩保留绝对路径，避免别的路径坑
    config_dict["tileDir"] = str(REPO_ROOT / "tiles") + "/"
    config_dict["outputDir"] = str(run_output_dir) + "/"

    cfg_path = run_dir / "config.json"
    cfg_path.write_text(json.dumps(config_dict, indent=2), encoding="utf-8")

    cmd = [str(EXE), A, B, str(cfg_path)]
    p = subprocess.run(
        cmd,
        cwd=str(REPO_ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    out = p.stdout
    stdout_path = run_dir / "stdout.txt"
    stdout_path.write_text(out, encoding="utf-8")

    metrics = {
        "total_cycle": grab(r"total cycle\s*=\s*([0-9]+)", out),
        "dram_A_total": grab(r"total DRAM access A\s*=\s*([0-9]+)", out),
        "dram_B_total": grab(r"total DRAM access B\s*=\s*([0-9]+)", out),
        "dram_C_total": grab(r"total DRAM access C\s*=\s*([0-9]+)", out),
        "hitrate": grab(r"hitrate\s*=\s*([0-9]*\.?[0-9]+)", out),
        "returncode": p.returncode,
        "stdout_path": str(stdout_path),
        "config_path": str(cfg_path),
    }
    return metrics


def main():
    print("=== SeaCache Sweep ===")
    print("Repo root:", REPO_ROOT)
    print("Executable exists?", EXE.exists())
    print("Base config exists?", BASE_CONFIG.exists())
    print("Data file exists?", (REPO_ROOT / "data" / "filter3D.mtx").exists())
    print("Matrix args:", A, B)
    print()

    base_cfg = json.loads(BASE_CONFIG.read_text(encoding="utf-8"))

    fieldnames = [
        "timestamp",
        "matrixA",
        "matrixB",
        "cachesize",
        "memorybandwidth",
        "PEcnt",
        "srambank",
        "total_cycle",
        "dram_A_total",
        "dram_B_total",
        "dram_C_total",
        "hitrate",
        "returncode",
        "stdout_path",
        "config_path",
    ]

    new_file = not CSV_PATH.exists()
    with open(CSV_PATH, "a", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        if new_file:
            writer.writeheader()

        for cs in CACHE_SIZES:
            cfg = dict(base_cfg)
            cfg["cachesize"] = cs

            run_name = f"cs{cs}_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
            print(f"Running cachesize={cs} ...")

            m = run_once(cfg, run_name)

            row = {
                "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
                "matrixA": A,
                "matrixB": B,
                "cachesize": cs,
                "memorybandwidth": cfg.get("memorybandwidth", ""),
                "PEcnt": cfg.get("PEcnt", ""),
                "srambank": cfg.get("srambank", ""),
                **m,
            }
            writer.writerow(row)

            print("  total_cycle =", m["total_cycle"], "hitrate =", m["hitrate"])
            if m["total_cycle"] == "" and "Error opening input file" in Path(m["stdout_path"]).read_text(encoding="utf-8"):
                print("  still input error; open stdout.txt")
            print()

    print("Saved:", CSV_PATH)


if __name__ == "__main__":
    main()