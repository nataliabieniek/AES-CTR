"""Wykresy i tabela zbiorcza z results/cuda_bench.csv (patrz bench_cuda.py).

    python python/plot_cuda.py [CSV]     # -> results/plots/cuda_*.png, results/cuda_summary.csv

Punkty to mediany z powtorzen, wasy pokazuja min..max. Przepustowosc liczymy
z bajtow i sekund (kolumny GB/s w CSV sa zaokraglone do 3 miejsc).
Czasy h2d/kernel/d2h to czasy zdarzen CUDA; przy podwojnym buforowaniu nakladaja
sie na siebie i na I/O, wiec ich suma moze przekraczac total_s.
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
    "sync": ("synchroniczny (1 slot)", "#2a78d6", "o"),
    "double": ("podwójne buforowanie (2 sloty)", "#1baf7a", "^"),
}
PHASES = [("read_s", "odczyt pliku", "#8e8d89"), ("h2d_s", "H2D", "#eb6834"),
          ("kernel_s", "kernel", "#2a78d6"), ("d2h_s", "D2H", "#e0b030"),
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
    xkey = {"buffer": "buffer", "filesize": "bytes"}
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


def plot_throughput(groups, experiment, xlabel, out):
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.4), sharey=True)
    for ax, (field, title) in zip(axes, (("kernel_s", "jądro (kernel)"), ("total_s", "cały potok"))):
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
    """Mediany czasow faz (odczyt, H2D, kernel, D2H, zapis) dla rozmiarow partii, obok total_s."""
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.6), sharey=True)
    for ax, mode in zip(axes, MODES):
        xs = sorted(x for (e, m, x) in groups if e == "buffer" and m == mode)
        if not xs:
            continue
        pos = range(len(xs))
        bottom = [0.0] * len(xs)
        for field, label, colour in PHASES:
            vals = [statistics.median(groups[("buffer", mode, x)][field]) for x in xs]
            ax.bar(pos, vals, bottom=bottom, label=label, color=colour, width=0.6)
            bottom = [b + v for b, v in zip(bottom, vals)]
        ax.plot(pos, [statistics.median(groups[("buffer", mode, x)]["total_s"]) for x in xs],
                color=INK, marker="D", linestyle="--", linewidth=1.2, label="total_s (wall)")
        ax.set_xticks(list(pos))
        ax.set_xticklabels([human(x) for x in xs], rotation=30, ha="right")
        ax.set_title(f"Czas faz - {MODES[mode][0]}")
        ax.set_xlabel("rozmiar partii")
    axes[0].set_ylabel("s (suma faz)")
    fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=6)
    fig.tight_layout(rect=(0, 0.1, 1, 1))
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
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results" / "cuda_bench.csv"
    groups = load(src)
    plots = src.parent / "plots"
    plots.mkdir(exist_ok=True)
    plot_throughput(groups, "buffer", "rozmiar partii (bufora)", plots / "cuda_buffer.png")
    plot_throughput(groups, "filesize", "rozmiar pliku", plots / "cuda_filesize.png")
    plot_phases(groups, plots / "cuda_phases.png")
    write_summary(groups, src.with_name("cuda_summary.csv"))
    print(f"wykresy: {plots}/cuda_*.png\ntabela:  {src.with_name('cuda_summary.csv')}")


if __name__ == "__main__":
    main()
