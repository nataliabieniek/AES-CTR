"""Pomiary OpenMP: rozgrzewka + powtorzenia, surowe wyniki do CSV.

    python python/bench_openmp.py              # pelny przebieg -> results/openmp_bench.csv
    python python/bench_openmp.py --quick      # szybki test (2 powtorzenia, male pliki)

Eksperymenty (kolumna `experiment`):
    threads   plik 100 MB, bufor 1 MiB, rozna liczba watkow  -> S(p), E(p)
    buffer    plik 100 MB, wszystkie watki, rozny rozmiar bufora
    filesize  wszystkie pliki z data/, wszystkie watki, bufor 1 MiB
Kazdy dla trzech potokow (kolumna `mode`):
    sync-static   --double 0, OMP_SCHEDULE=static
    sync-dynamic  --double 0, OMP_SCHEDULE=dynamic,256
    double        --double 1 (szyfrowanie w zadaniach przydzielanych dynamicznie)
`double` rozdziela prace dynamicznie, wiec porownujemy go z sync-dynamic, a nie
z sync-static: roznica to wtedy samo nakladanie I/O, a nie sposob przydzialu.
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
BIN = ROOT / "build" / "aes_ctr_openmp"
DATA = ROOT / "data"
KEY = "2b7e151628aed2a6abf7158809cf4f3c"
CTR = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
MIB = 1 << 20

MODES = {
    "sync-static": (0, "static"),
    "sync-dynamic": (0, "dynamic,256"),
    "double": (1, None),
}
FIELDS = ["experiment", "mode", "file", "threads", "run", "buffer", "bytes", "read_s",
          "kernel_s", "write_s", "total_s", "kernel_GBps", "total_GBps"]


def run_once(path, out, threads, buffer, mode):
    double, sched = MODES[mode]
    env = dict(os.environ)
    env.pop("OMP_SCHEDULE", None)
    if sched:
        env["OMP_SCHEDULE"] = sched
    cmd = [str(BIN), "--in", str(path), "--out", str(out), "--key", KEY, "--counter", CTR,
           "--buffer", str(buffer), "--threads", str(threads), "--double", str(double)]
    line = subprocess.run(cmd, env=env, check=True, capture_output=True, text=True).stdout
    return dict(kv.split("=", 1) for kv in line.split())


def machine_info():
    info = {"platform": platform.platform(), "machine": platform.machine(),
            "logical_cpus": os.cpu_count(), "python": platform.python_version()}
    probes = {  # tylko macOS; na innych systemach pomijane
        "cpu": ["sysctl", "-n", "machdep.cpu.brand_string"],
        "performance_cores": ["sysctl", "-n", "hw.perflevel0.physicalcpu"],
        "efficiency_cores": ["sysctl", "-n", "hw.perflevel1.physicalcpu"],
        "memory_bytes": ["sysctl", "-n", "hw.memsize"],
    }
    for name, cmd in probes.items():
        try:
            info[name] = subprocess.run(cmd, capture_output=True, text=True, check=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            pass
    return info


def main():
    ncpu = os.cpu_count() or 1
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs", type=int, default=5, help="liczba pomiarow na konfiguracje")
    ap.add_argument("--warmup", type=int, default=1, help="przebiegi rozgrzewkowe (odrzucane)")
    ap.add_argument("--threads", type=int, nargs="+",
                    default=sorted({t for t in (1, 2, 4, 6, 8, 10, 12, 16) if t < ncpu} | {ncpu}))
    ap.add_argument("--out", default=str(ROOT / "results" / "openmp_bench.csv"))
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    if not BIN.exists():
        sys.exit("brak build/aes_ctr_openmp - najpierw `make openmp`")
    big = DATA / ("file_10MB.bin" if args.quick else "file_100MB.bin")
    files = sorted(DATA.glob("file_*.bin"), key=lambda p: p.stat().st_size)
    if not big.exists() or not files:
        sys.exit("brak plikow wejsciowych - najpierw `python python/gen_data.py`")
    if args.quick:
        args.runs, files = 2, files[:3]
    tmax = max(args.threads)

    configs = []  # (eksperyment, plik, watki, bufor)
    configs += [("threads", big, t, MIB) for t in args.threads]
    configs += [("buffer", big, tmax, b) for b in (16384, 65536, MIB, 16 * MIB, 64 * MIB)]
    configs += [("filesize", f, tmax, MIB) for f in files]

    out_csv = Path(args.out)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    env_md = out_csv.with_name(out_csv.stem + "_env.md")
    env_md.write_text("".join(f"- {k}: {v}\n" for k, v in machine_info().items()))

    with tempfile.TemporaryDirectory() as tmp, open(out_csv, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        enc = Path(tmp) / "out.bin"
        for exp, path, threads, buffer in configs:
            for mode in MODES:
                for _ in range(args.warmup):
                    run_once(path, enc, threads, buffer, mode)
                rows = []
                for r in range(args.runs):
                    row = run_once(path, enc, threads, buffer, mode)
                    row.update(experiment=exp, mode=mode, file=path.name, threads=threads, run=r)
                    w.writerow(row)
                    rows.append(float(row["kernel_GBps"]))
                fh.flush()
                rows.sort()
                print(f"{exp:8} {mode:12} {path.name:15} t={threads:<2} buf={buffer:<9}"
                      f" jadro GB/s mediana={statistics.median(rows):.3f} min={rows[0]:.3f} max={rows[-1]:.3f}")
    print(f"\nsurowe wyniki: {out_csv}\nmaszyna:       {env_md}")


if __name__ == "__main__":
    main()
