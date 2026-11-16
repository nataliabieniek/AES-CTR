"""Wykresy i tabela zbiorcza z results/openmp_bench.csv (patrz bench_openmp.py).

    python python/plot_openmp.py [CSV]     # -> results/plots/openmp_*.png, results/openmp_summary.csv

Punkty to mediany z powtorzen, wasy pokazuja min..max. Przepustowosc liczymy
z bajtow i sekund: kolumny GB/s w CSV sa zaokraglone do 3 miejsc, co dla malych
plikow jest zbyt zgrubne.
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
    "sync-static": ("synchroniczny, schedule(static)", "#2a78d6", "o"),
    "sync-dynamic": ("synchroniczny, schedule(dynamic,256)", "#eb6834", "s"),
    "double": ("podwójne buforowanie", "#1baf7a", "^"),
}
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e4e3df"
plt.rcParams.update({
    "figure.facecolor": "#fcfcfb", "axes.facecolor": "#fcfcfb", "savefig.facecolor": "#fcfcfb",
    "axes.edgecolor": GRID, "axes.labelcolor": MUTED, "xtick.color": MUTED, "ytick.color": MUTED,
    "text.color": INK, "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.8,
    "axes.spines.top": False, "axes.spines.right": False, "font.size": 10,
    "axes.titlesize": 11, "axes.titleweight": "bold", "axes.titlelocation": "left",
    "legend.frameon": False, "lines.linewidth": 2, "lines.markersize": 6,
})


def load(path):
    """{(experiment, mode, x): {"kernel_s": [...], "total_s": [...], "bytes": n}}"""
    groups = defaultdict(lambda: defaultdict(list))
    xkey = {"threads": "threads", "buffer": "buffer", "filesize": "bytes"}
    with open(path, newline="") as fh:
        for r in csv.DictReader(fh):
            g = groups[(r["experiment"], r["mode"], int(r[xkey[r["experiment"]]]))]
            g["kernel_s"].append(float(r["kernel_s"]))
            g["total_s"].append(float(r["total_s"]))
            g["bytes"] = int(r["bytes"])
    return groups


def series(groups, experiment, mode, value):
    """Posortowane x oraz (mediana, min, max) wartosci value(grupa) dla kazdego x."""
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
    """S(p) = T(1)/T(p), E(p) = S(p)/p; T(1) = mediana czasu tego samego potoku na 1 watku."""
    fig, axes = plt.subplots(2, 2, figsize=(10, 7.5), sharex=True)
    for col, (field, title) in enumerate((("kernel_s", "jądro AES-CTR"), ("total_s", "cały potok (z I/O)"))):
        threads = []
        for mode in MODES:
            base = groups.get(("threads", mode, 1))
            if not base:
                continue
            t1 = statistics.median(base[field])
            threads, s_med, s_lo, s_hi = series(groups, "threads", mode, lambda g: [t1 / s for s in g[field]])
            draw(axes[0][col], threads, s_med, s_lo, s_hi, mode)
            eff = [[v / p for v, p in zip(vals, threads)] for vals in (s_med, s_lo, s_hi)]
            draw(axes[1][col], threads, *eff, mode)
        if threads:
            axes[0][col].plot(threads, threads, color=MUTED, linestyle="--", linewidth=1, label="idealne")
            axes[1][col].axhline(1.0, color=MUTED, linestyle="--", linewidth=1)
            axes[1][col].set_xticks(threads)
        axes[0][col].set_title(f"Przyspieszenie S(p) - {title}")
        axes[1][col].set_title(f"Efektywność E(p) - {title}")
        axes[1][col].set_xlabel("liczba wątków p")
        axes[1][col].set_ylim(0, 1.15)
    fig.legend(*axes[0][0].get_legend_handles_labels(), loc="lower center", ncol=4)
    fig.tight_layout(rect=(0, 0.05, 1, 1))
    fig.savefig(out, dpi=150)
    plt.close(fig)


def plot_throughput(groups, experiment, xlabel, out):
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.4), sharey=True)
    for ax, (field, title) in zip(axes, (("kernel_s", "jądro"), ("total_s", "cały potok"))):
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
    fig.legend(*axes[0].get_legend_handles_labels(), loc="lower center", ncol=3)
    fig.tight_layout(rect=(0, 0.08, 1, 1))
    fig.savefig(out, dpi=150)
    plt.close(fig)


def write_summary(groups, out):
    with open(out, "w", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["experiment", "mode", "x", "runs", "kernel_s_median", "kernel_s_min", "kernel_s_max",
                    "total_s_median", "kernel_GBps_median", "total_GBps_median"])
        for (exp, mode, x), g in sorted(groups.items()):
            k, t = statistics.median(g["kernel_s"]), statistics.median(g["total_s"])
            w.writerow([exp, mode, x, len(g["kernel_s"]), f"{k:.6f}", f"{min(g['kernel_s']):.6f}",
                        f"{max(g['kernel_s']):.6f}", f"{t:.6f}",
                        f"{g['bytes'] / 1e9 / k:.4f}" if k else "", f"{g['bytes'] / 1e9 / t:.4f}" if t else ""])


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "results" / "openmp_bench.csv"
    groups = load(src)
    plots = src.parent / "plots"
    plots.mkdir(exist_ok=True)
    plot_scaling(groups, plots / "openmp_scaling.png")
    plot_throughput(groups, "buffer", "rozmiar bufora", plots / "openmp_buffer.png")
    plot_throughput(groups, "filesize", "rozmiar pliku", plots / "openmp_filesize.png")
    write_summary(groups, src.with_name("openmp_summary.csv"))
    print(f"wykresy: {plots}/openmp_*.png\ntabela:  {src.with_name('openmp_summary.csv')}")


if __name__ == "__main__":
    main()
