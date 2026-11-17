"""Pomiary CUDA: rozgrzewka + powtorzenia, surowe wyniki do CSV.

    python python/bench_cuda.py              # pelny przebieg -> results/cuda_bench.csv
    python python/bench_cuda.py --quick      # szybki test (2 powtorzenia, male pliki)

Eksperymenty (kolumna `experiment`):
    buffer    plik 100 MB, rozny rozmiar partii (bufora)
    filesize  wszystkie pliki z data/, bufor 16 MiB
Kazdy dla dwoch potokow (kolumna `mode`):
    sync    --double 0, jeden slot: read -> H2D -> kernel -> D2H -> write
    double  --double 1, dwa sloty: transfery i I/O nakladaja sie na kernel
Kolumny h2d_s / kernel_s / d2h_s pochodza ze zdarzen CUDA, wiec pokazuja, kiedy
transfery i I/O niwelują przewage GPU (kernel_GBps vs total_GBps).
"""
import argparse
import csv
import os
import platform
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "aes_ctr_cuda"
DATA = ROOT / "data"
KEY = "2b7e151628aed2a6abf7158809cf4f3c"
CTR = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
MIB = 1 << 20

MODES = {"sync": 0, "double": 1}
FIELDS = ["experiment", "mode", "file", "run", "buffer", "bytes", "read_s", "h2d_s",
          "kernel_s", "d2h_s", "write_s", "total_s", "kernel_GBps", "total_GBps"]


def run_once(path, out, buffer, mode):
    cmd = [str(BIN), "--in", str(path), "--out", str(out), "--key", KEY, "--counter", CTR,
           "--buffer", str(buffer), "--double", str(MODES[mode])]
    line = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    return dict(kv.split("=", 1) for kv in line.split())


def machine_info():
    info = {"platform": platform.platform(), "machine": platform.machine(),
            "logical_cpus": os.cpu_count(), "python": platform.python_version()}
    probes = {
        "gpu": ["nvidia-smi", "--query-gpu=name,driver_version,memory.total,pcie.link.gen.current,"
                "pcie.link.width.current", "--format=csv,noheader"],
        "nvcc": ["nvcc", "--version"],
    }
    for name, cmd in probes.items():
        try:
            info[name] = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.strip().splitlines()[-1]
        except (OSError, subprocess.CalledProcessError, IndexError):
            pass
    return info


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs", type=int, default=5, help="liczba pomiarow na konfiguracje")
    ap.add_argument("--warmup", type=int, default=1, help="przebiegi rozgrzewkowe (odrzucane)")
    ap.add_argument("--out", default=str(ROOT / "results" / "cuda_bench.csv"))
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    if not BIN.exists():
        sys.exit("brak build/aes_ctr_cuda - najpierw `make cuda`")
    big = DATA / ("file_10MB.bin" if args.quick else "file_100MB.bin")
    files = sorted(DATA.glob("file_*.bin"), key=lambda p: p.stat().st_size)
    if not big.exists() or not files:
        sys.exit("brak plikow wejsciowych - najpierw `python python/gen_data.py`")
    if args.quick:
        args.runs, files = 2, files[:3]

    configs = []  # (eksperyment, plik, bufor)
    configs += [("buffer", big, b) for b in (16384, 65536, MIB, 4 * MIB, 16 * MIB, 64 * MIB)]
    configs += [("filesize", f, 16 * MIB) for f in files]

    out_csv = Path(args.out)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    env_md = out_csv.with_name(out_csv.stem + "_env.md")
    env_md.write_text("".join(f"- {k}: {v}\n" for k, v in machine_info().items()))

    with tempfile.TemporaryDirectory() as tmp, open(out_csv, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        enc = Path(tmp) / "out.bin"
        for exp, path, buffer in configs:
            for mode in MODES:
                for _ in range(args.warmup):
                    run_once(path, enc, buffer, mode)
                rows = []
                for r in range(args.runs):
                    row = run_once(path, enc, buffer, mode)
                    row.update(experiment=exp, mode=mode, file=path.name, run=r)
                    w.writerow(row)
                    rows.append(float(row["total_GBps"]))
                fh.flush()
                rows.sort()
                print(f"{exp:8} {mode:7} {path.name:15} buf={buffer:<9}"
                      f" potok GB/s mediana={statistics.median(rows):.3f} min={rows[0]:.3f} max={rows[-1]:.3f}")
    print(f"\nsurowe wyniki: {out_csv}\nmaszyna:       {env_md}")


if __name__ == "__main__":
    main()
