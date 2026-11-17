"""Pomiary MPI: rozgrzewka + powtorzenia, surowe wyniki do CSV.

    python python/bench_mpi.py              # pelny przebieg -> results/mpi_bench.csv
    python python/bench_mpi.py --quick      # szybki test (2 powtorzenia, male pliki)
    python python/bench_mpi.py --hostfile hosts --weights 6,1 --procs 2 --out results/mpi_nodes_bench.csv
                                            # kilka maszyn (np. BeagleBone): jeden proces na maszyne

Eksperymenty (kolumna `experiment`):
    procs     plik 100 MB, bufor 1 MiB, rozna liczba procesow  -> S(p), E(p)
    buffer    plik 100 MB, wszystkie procesy, rozny rozmiar partii
    filesize  wszystkie pliki z data/, wszystkie procesy, bufor 1 MiB
Kazdy dla dwoch potokow (kolumna `mode`):
    sync    --double 0: read -> Scatterv -> szyfrowanie -> Gatherv -> write
    double  --double 1: nieblokujace Iscatterv/Igatherv nakladane na szyfrowanie
Kolumny h2d_s (dystrybucja) i d2h_s (zbieranie) to koszt komunikacji; kernel_s to najwolniejsza ranga.
"""
import argparse
import csv
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "aes_ctr_mpi"
DATA = ROOT / "data"
KEY = "2b7e151628aed2a6abf7158809cf4f3c"
CTR = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"
MIB = 1 << 20

MODES = {"sync": 0, "double": 1}
FIELDS = ["experiment", "mode", "file", "procs", "run", "buffer", "bytes", "read_s", "h2d_s",
          "kernel_s", "d2h_s", "write_s", "total_s", "kernel_GBps", "total_GBps"]


def run_once(path, out, procs, buffer, mode, hostfile, weights):
    cmd = ["mpirun", "--allow-run-as-root", "--oversubscribe", "-n", str(procs)]
    if hostfile:
        cmd += ["--hostfile", hostfile]
    if weights:
        cmd += ["-x", f"AES_MPI_WEIGHTS={weights}"]
    cmd += [str(BIN), "--in", str(path), "--out", str(out), "--key", KEY, "--counter", CTR,
            "--buffer", str(buffer), "--double", str(MODES[mode])]
    line = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    return dict(kv.split("=", 1) for kv in line.split())


def machine_info(args):
    info = {"platform": platform.platform(), "machine": platform.machine(),
            "logical_cpus": os.cpu_count(), "python": platform.python_version(),
            "hostfile": args.hostfile or "brak (jedna maszyna)", "weights": args.weights or "rowne"}
    try:
        info["mpirun"] = subprocess.run(["mpirun", "--version"], capture_output=True,
                                        text=True, check=True).stdout.splitlines()[0]
    except (OSError, subprocess.CalledProcessError, IndexError):
        pass
    return info


def main():
    ncpu = os.cpu_count() or 1
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runs", type=int, default=5, help="liczba pomiarow na konfiguracje")
    ap.add_argument("--warmup", type=int, default=1, help="przebiegi rozgrzewkowe (odrzucane)")
    ap.add_argument("--procs", type=int, nargs="+",
                    default=sorted({p for p in (1, 2, 4, 6, 8, 12, 16) if p < ncpu} | {ncpu}))
    ap.add_argument("--hostfile", help="plik hostfile OpenMPI (pomiar na kilku maszynach)")
    ap.add_argument("--weights", help="wagi rang, np. 6,1 (AES_MPI_WEIGHTS); liczba wag = liczba procesow")
    ap.add_argument("--out", default=str(ROOT / "results" / "mpi_bench.csv"))
    ap.add_argument("--quick", action="store_true")
    args = ap.parse_args()

    if not BIN.exists():
        sys.exit("brak build/aes_ctr_mpi - najpierw `make mpi`")
    if not shutil.which("mpirun"):
        sys.exit("brak mpirun")
    if args.weights and len(args.procs) > 1:
        sys.exit("--weights wymaga jednej liczby procesow (--procs N)")
    big = DATA / ("file_10MB.bin" if args.quick else "file_100MB.bin")
    files = sorted(DATA.glob("file_*.bin"), key=lambda p: p.stat().st_size)
    if not big.exists() or not files:
        sys.exit("brak plikow wejsciowych - najpierw `python python/gen_data.py`")
    if args.quick:
        args.runs, files = 2, files[:3]
    pmax = max(args.procs)

    configs = []  # (eksperyment, plik, procesy, bufor)
    configs += [("procs", big, p, MIB) for p in args.procs]
    configs += [("buffer", big, pmax, b) for b in (16384, 65536, MIB, 16 * MIB, 64 * MIB)]
    configs += [("filesize", f, pmax, MIB) for f in files]

    out_csv = Path(args.out)
    out_csv.parent.mkdir(parents=True, exist_ok=True)
    env_md = out_csv.with_name(out_csv.stem + "_env.md")
    env_md.write_text("".join(f"- {k}: {v}\n" for k, v in machine_info(args).items()))

    with tempfile.TemporaryDirectory() as tmp, open(out_csv, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        enc = Path(tmp) / "out.bin"
        for exp, path, procs, buffer in configs:
            for mode in MODES:
                for _ in range(args.warmup):
                    run_once(path, enc, procs, buffer, mode, args.hostfile, args.weights)
                rows = []
                for r in range(args.runs):
                    row = run_once(path, enc, procs, buffer, mode, args.hostfile, args.weights)
                    row.update(experiment=exp, mode=mode, file=path.name, procs=procs, run=r)
                    w.writerow(row)
                    rows.append(float(row["total_GBps"]))
                fh.flush()
                rows.sort()
                print(f"{exp:8} {mode:7} {path.name:15} p={procs:<2} buf={buffer:<9}"
                      f" potok GB/s mediana={statistics.median(rows):.3f} min={rows[0]:.3f} max={rows[-1]:.3f}")
    print(f"\nsurowe wyniki: {out_csv}\nmaszyna:       {env_md}")


if __name__ == "__main__":
    main()
