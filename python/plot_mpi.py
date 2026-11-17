"""Wykresy i tabela zbiorcza z results/mpi_bench.csv (patrz bench_mpi.py).

    python python/plot_mpi.py [CSV]     # -> results/plots/mpi_*.png, results/mpi_summary.csv

Punkty to mediany z powtorzen, wasy pokazuja min..max. Przepustowosc liczymy
z bajtow i sekund. Czasy faz to maksimum po rangach: h2d_s = dystrybucja (Scatterv),
d2h_s = zbieranie (Gatherv); przy podwojnym buforowaniu nakladaja sie na obliczenia.
Prefiks plikow wynikowych (mpi_*) bierzemy z nazwy CSV (np. mpi_nodes_bench.csv -> mpi_nodes_*).
"""
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
MODES = {  # etykieta, kolor, znacznik
    "sync": ("synchroniczny", "#2a78d6", "o"),
    "double": ("podwójne buforowanie (Iscatterv/Igatherv)", "#1baf7a", "^"),
}
PHASES = [("read_s", "odczyt pliku", "#8e8d89"), ("h2d_s", "dystrybucja (Scatterv)", "#eb6834"),
          ("kernel_s", "szyfrowanie", "#2a78d6"), ("d2h_s", "zbieranie (Gatherv)", "#e0b030"),
          ("write_s", "zapis pliku", "#1baf7a")]
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e4e3df"
plt.rcParams.update({
    "figure.facecolor": "#fcfcfb", "axes.facecolor": "#fcfcfb", "savefig.facecolor": "#fcfcfb",
    "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "xtick.color": MUTED, "ytick.color": MUTED,
    "text.color": INK, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False, "font.size": 10,
    "axes.titlesize": 11, "axes.titleweight": "bold", "axes.titlelocation": "left",
    "legend.frameon": False, "lines.linewidth": 2, "lines.markersize": 6,
})
FIELDS = ["read_s", "h2d_s", "kernel_s", "d2h_s", "write_s", "total_s"]


def load(path):
    """{(experiment, mode, x): {pole: [...], "bytes": n}}"""
    groups = defaultdict(lambda: defaultdict(list))
    xkey = {"procs": "procs", "buffer": "buffer", "filesize": "bytes"}
    with open(path, newline="") as fh:
        for r in csv.DictReader(fh):
            g = groups[(r["experiment"], r["mode"], int(r[xkey[r["experiment"]]]))]
            for f in FIELDS:
                g[f].append(float(r[f]))
            g["bytes"] = int(r["bytes"])
    return groups


def series(groups, experiment, mode, value):
    xs = sorted(x for (e, m, x) in groups if e == experiment and m == mode)
    med, lo, hi = [], [], []
    for x in xs:
        vals = value(groups[(experiment, mode, x)])
        med.append(statistics.median(vals))
        lo.append(min(vals))
        hi.append(max(vals))
    return xs, med, lo, hi


def draw(ax, xs, med, lo, hi, mode):
    label, colour, marker = MODES[mode]
    err = [[m - a for m, a in zip(med, lo)], [b - m for m, b in zip(med, hi)]]
    ax.errorbar(xs, med, yerr=err, label=label, color=colour, marker=marker,
                capsize=3, elinewidth=1, markeredgecolor="#fcfcfb", markeredgewidth=1)


def gbps(field):
    return lambda g: [g["bytes"] / 1e9 / s if s > 0 else 0.0 for s in g[field]]


def human(n):
    for unit, size in (("GiB", 1 << 30), ("MiB", 1 << 20), ("KiB", 1 << 10)):
        if n >= size:
            return f"{n / size:.3g} {unit}"
    return f"{n} B"


def plot_scaling(groups, out):
    """S(p) = T(1)/T(p), E(p) = S(p)/p; T(1) = mediana czasu tego samego potoku na 1 procesie."""
    fig, axes = plt.subplots(2, 2, figsize=(10, 7.5), sharex=True)
    for col, (field, title) in enumerate((("kernel_s", "szyfrowanie"), ("total_s", "cały potok (z I/O i komunikacją)"))):
        procs = []
        for mode in MODES:
            base = groups.get(("procs", mode, 1))
            if not base:
                continue
            t1 = statistics.median(base[field])
            procs, s_med, s_lo, s_hi = series(groups, "procs", mode, lambda g: [t1 / s for s in g[field]])
            draw(axes[0][col], procs, s_med, s_lo, s_hi, mode)
            eff = [[v / p for v, p in zip(vals, procs)] for vals in (s_med, s_lo, s_hi)]
            draw(axes[1][col], procs, *eff, mode)
        if procs:
            axes[0][col].plot(procs, procs, color=MUTED, linestyle="--", linewidth=1, label="idealne")
            axes[1][col].axhline(1.0, color=MUTED, linestyle="--", linewidth=1)
            axes[1][col].set_xticks(procs)
        axes[0][col].set_title(f"Przyspieszenie S(p) - {title}")
        axes[1][col].set_title(f"Efektywność E(p) - {title}")
        axes[1][col].set_xlabel("liczba procesów p")
        axes[1][col].set_ylim(0, 1.15)
    fig.legend(*axes[0][0].get_legend_handles_labels(), loc="lower center", ncol=3)
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    fig.savefig(out, dpi=150)
    plt.close(fig)


def plot_throughput(groups, experiment, xlabel, out):
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.4), sharey=True)
    for ax, (field, title) in zip(axes, (("kernel_s", "szyfrowanie"), ("total_s", "cały potok"))):
        xs = []
        for mode in MODES:
            xs, med, lo, hi = series(groups, experiment, mode, gbps(field))
            if xs:
                draw(ax, xs, med, lo, hi, mode)
        ax.set_xscale("log", base=2)
        if xs:
            ax.set_xticks(xs)
            ax.set_xticklabels([human(x) for x in xs], rotation=30, ha="right")
        ax.minorticks_off()
        ax.set_title(f"Przepustowość - {title}")
        ax.set_xlabel(xlabel)
        ax.set_ylim(bottom=0)
    axes[0].set_ylabel("GB/s")
    fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=2)
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    fig.savefig(out, dpi=150)
    plt.close(fig)


def plot_phases(groups, out):
    """Czasy faz dla kolejnych liczb procesow (plik 100 MB): koszt dystrybucji i zbierania vs szyfrowanie."""
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.6), sharey=True)
    for ax, mode in zip(axes, MODES):
        xs = sorted(x for (e, m, x) in groups if e == "procs" and m == mode)
        if not xs:
            continue
        pos = range(len(xs))
        bottom = [0.0] * len(xs)
        for field, label, colour in PHASES:
            vals = [statistics.median(groups[("procs", mode, x)][field]) for x in xs]
            ax.bar(pos, vals, bottom=bottom, label=label, color=colour, width=0.6)
            bottom = [b + v for b, v in zip(bottom, vals)]
        ax.plot(pos, [statistics.median(groups[("procs", mode, x)]["total_s"]) for x in xs],
                color=INK, marker="D", linestyle="--", linewidth=1.2, label="total_s (wall)")
        ax.set_xticks(list(pos))
        ax.set_xticklabels([str(x) for x in xs])
        ax.set_title(f"Czas faz - {MODES[mode][0]}")
        ax.set_xlabel("liczba procesów p")
    axes[0].set_ylabel("s (suma faz)")
    fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=3)
    fig.tight_layout(rect=(0, 0.12, 1, 1))
    fig.savefig(out, dpi=150)
    plt.close(fig)


def write_summary(groups, out):
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["experiment", "mode", "x", "runs", "read_s", "h2d_s", "kernel_s", "d2h_s", "write_s",
                    "total_s_median", "total_s_min", "total_s_max", "kernel_GBps_median", "total_GBps_median"])
        for (exp, mode, x), g in sorted(groups.items()):
            med = {f: statistics.median(g[f]) for f in FIELDS}
            k, t = med["kernel_s"], med["total_s"]
            w.writerow([exp, mode, x, len(g["total_s"])]
                       + [f"{med[f]:.6f}" for f in ("read_s", "h2d_s", "kernel_s", "d2h_s", "write_s")]
                       + [f"{t:.6f}", f"{min(g['total_s']):.6f}", f"{max(g['total_s']):.6f}",
                          f"{g['bytes'] / 1e9 / k:.4f}" if k else "", f"{g['bytes'] / 1e9 / t:.4f}" if t else ""])


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results" / "mpi_bench.csv"
    prefix = src.stem.removesuffix("_bench")
    groups = load(src)
    plots = src.parent / "plots"
    plots.mkdir(exist_ok=True)
    plot_scaling(groups, plots / f"{prefix}_scaling.png")
    plot_throughput(groups, "buffer", "rozmiar partii (bufora)", plots / f"{prefix}_buffer.png")
    plot_throughput(groups, "filesize", "rozmiar pliku", plots / f"{prefix}_filesize.png")
    plot_phases(groups, plots / f"{prefix}_phases.png")
    summary = src.with_name(f"{prefix}_summary.csv")
    write_summary(groups, summary)
    print(f"wykresy: {plots}/{prefix}_*.png\ntabela:  {summary}")


if __name__ == "__main__":
    main()
