#!/usr/bin/env python3
"""bench/plot.py - figures for the performance study.

Modes (chosen by flag; default --strong):
    --strong     speedup, efficiency and runtime (with/without communication)
                 versus rank count            (reads bench/results.csv)
    --scaling-n  runtime (with/without communication) versus problem size N
                 at a fixed rank count        (reads bench/scaling_n.csv)
    --gran       per-process compute/communication stacked bar and the
                 idle-time load-balance check (reads bench/timing.csv)
    --weak       weak-scaling efficiency       (reads bench/weak.csv)
    --hybrid     MPI x OpenMP grid             (reads bench/hybrid.csv)

Usage:
    python3 bench/plot.py bench/results.csv --strong

Text tables are always written so the figures can be reproduced even without
matplotlib; the PNGs are written when matplotlib is available.
"""
import csv, sys, os

# Okabe-Ito colourblind-safe palette.
OKABE = {
    "black":  "#000000", "orange": "#E69F00", "skyblue": "#56B4E9",
    "green":  "#009E73", "yellow": "#F0E442", "blue":    "#0072B2",
    "vermil": "#D55E00", "purple": "#CC79A7", "grey":    "#999999",
}


def read_csv(path):
    with open(path) as f:
        return list(csv.DictReader(f))


def _plt():
    """Return pyplot or None (text-only fallback)."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        return plt
    except ImportError:
        print("matplotlib not installed; wrote text table only", file=sys.stderr)
        return None


# ---------------------------------------------------------------- strong
def strong_plots(rows, outdir):
    rows = sorted(rows, key=lambda r: int(r["np"]))
    nps   = [int(r["np"]) for r in rows]
    total = [float(r["total_s"]) for r in rows]
    comm  = [float(r.get("comm_s", 0.0)) for r in rows]
    comp  = [t - c for t, c in zip(total, comm)]
    t1    = total[0]
    speed = [t1 / t for t in total]
    eff   = [s / p for s, p in zip(speed, nps)]

    txt = os.path.join(outdir, "strong_scaling.txt")
    with open(txt, "w") as f:
        f.write("np\ttotal_s\tcomm_s\tcompute_s\tspeedup\tefficiency\n")
        for p, t, c, k, s, e in zip(nps, total, comm, comp, speed, eff):
            f.write(f"{p}\t{t:.4f}\t{c:.4f}\t{k:.4f}\t{s:.3f}\t{e:.3f}\n")
    print(f"wrote {txt}")

    plt = _plt()
    if plt is None:
        return

    # runtime with / without communication
    plt.figure()
    plt.plot(nps, total, "o-", color=OKABE["vermil"], label="with communication")
    plt.plot(nps, comp,  "s--", color=OKABE["green"], label="compute only")
    plt.xlabel("MPI processes"); plt.ylabel("wall time [s]")
    plt.title("Strong scaling: runtime vs processes")
    plt.legend(); plt.grid(True, alpha=0.3); plt.ylim(bottom=0)
    p = os.path.join(outdir, "strong_runtime.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")

    # speedup
    plt.figure()
    plt.plot(nps, speed, "o-", color=OKABE["blue"], label="measured")
    plt.plot(nps, nps,   "--", color=OKABE["grey"], label="ideal (linear)")
    plt.xlabel("MPI processes"); plt.ylabel("speedup  T(1)/T(p)")
    plt.title("Strong-scaling speedup"); plt.legend(); plt.grid(True, alpha=0.3)
    p = os.path.join(outdir, "speedup.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")

    # efficiency
    plt.figure()
    plt.plot(nps, eff, "o-", color=OKABE["green"])
    plt.axhline(1.0, ls="--", color=OKABE["grey"], alpha=0.7)
    plt.xlabel("MPI processes"); plt.ylabel("parallel efficiency  S/p")
    plt.ylim(0, 1.1); plt.title("Parallel efficiency"); plt.grid(True, alpha=0.3)
    p = os.path.join(outdir, "efficiency.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")


# ------------------------------------------------------------- scaling-n
def scaling_n_plots(rows, outdir):
    rows = sorted(rows, key=lambda r: int(r["n"]))
    ns    = [int(r["n"]) for r in rows]
    total = [float(r["total_s"]) for r in rows]
    comm  = [float(r.get("comm_s", 0.0)) for r in rows]
    comp  = [t - c for t, c in zip(total, comm)]

    txt = os.path.join(outdir, "scaling_n.txt")
    with open(txt, "w") as f:
        f.write("n\ttotal_s\tcomm_s\tcompute_s\n")
        for n, t, c, k in zip(ns, total, comm, comp):
            f.write(f"{n}\t{t:.4f}\t{c:.4f}\t{k:.4f}\n")
    print(f"wrote {txt}")

    plt = _plt()
    if plt is None:
        return
    plt.figure()
    plt.plot(ns, total, "o-", color=OKABE["vermil"], label="with communication")
    plt.plot(ns, comp,  "s--", color=OKABE["green"], label="compute only")
    # 2-3 minute target band
    plt.axhspan(120, 180, color=OKABE["yellow"], alpha=0.25, label="2-3 min target")
    plt.xlabel("number of bodies  N"); plt.ylabel("wall time [s]")
    plt.title("Runtime vs problem size (fixed rank count)")
    plt.legend(); plt.grid(True, alpha=0.3); plt.ylim(bottom=0)
    p = os.path.join(outdir, "scaling_n.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")


# ----------------------------------------------------------- granularity
def granularity_plots(rows, outdir):
    rows = sorted(rows, key=lambda r: int(r["rank"]))
    ranks = [int(r["rank"]) for r in rows]
    build = [float(r["build"]) for r in rows]
    force = [float(r["force"]) for r in rows]
    integ = [float(r["integ"]) for r in rows]
    comm  = [float(r["comm"])  for r in rows]
    comp  = [b + f + g for b, f, g in zip(build, force, integ)]

    # The blocking per-step all-gather synchronises every rank, so each rank's
    # TOTAL time is essentially identical and the imbalance shows up as uneven
    # COMPUTE time. A rank that finishes computing early then waits inside the
    # all-gather, which inflates its measured communication time. We therefore
    # estimate the true communication time as the smallest measured value (the
    # busiest rank waits least) and attribute the remainder to idle/wait time.
    comm_true = min(comm)
    busy = [k + comm_true for k in comp]            # real work, comm excluded of wait
    makespan = max(busy)
    idle = [makespan - bz for bz in busy]           # time each rank waits
    imbalance = (max(comp) - min(comp)) / max(comp) if max(comp) > 0 else 0.0
    balanced = imbalance <= 0.25

    txt = os.path.join(outdir, "granularity.txt")
    with open(txt, "w") as f:
        f.write("rank\tcompute_s\tcomm_s\tidle_s\n")
        for r, k, i in zip(ranks, comp, idle):
            f.write(f"{r}\t{k:.4f}\t{comm_true:.4f}\t{i:.4f}\n")
        f.write(f"# compute imbalance (max-min)/max = {imbalance*100:.1f}%  "
                f"-> {'BALANCED' if balanced else 'IMBALANCED (>25%)'}\n")
    print(f"wrote {txt}")
    print(f"compute imbalance = {imbalance*100:.1f}%  "
          f"({'balanced' if balanced else 'IMBALANCED: adjust granularity'})")

    plt = _plt()
    if plt is None:
        return
    plt.figure(figsize=(max(5, 0.5 * len(ranks)), 4))
    plt.bar(ranks, comp, color=OKABE["green"], label="compute (build+force+integrate)")
    plt.bar(ranks, [comm_true] * len(ranks), bottom=comp, color=OKABE["vermil"],
            label="communication")
    plt.bar(ranks, idle, bottom=[k + comm_true for k in comp],
            color=OKABE["grey"], alpha=0.6, label="idle (wait at all-gather)")
    plt.axhline(makespan, ls="--", color=OKABE["black"], alpha=0.5,
                label=f"makespan {makespan:.2f}s")
    plt.xlabel("process (MPI rank)"); plt.ylabel("time [s]")
    verdict = "balanced" if balanced else "imbalanced"
    plt.title(f"Per-process breakdown — compute imbalance {imbalance*100:.1f}% ({verdict})")
    plt.legend(fontsize=8); plt.grid(True, axis="y", alpha=0.3)
    plt.xticks(ranks)
    p = os.path.join(outdir, "granularity.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")


# -------------------------------------------------------------- energy
def energy_plots(rows, outdir):
    rows = sorted(rows, key=lambda r: int(r["step"]))
    steps = [int(r["step"]) for r in rows]
    E     = [float(r["E"]) for r in rows]
    e0    = E[0]
    ratio = [e / e0 for e in E]
    drift = (ratio[-1] - 1) * 100

    txt = os.path.join(outdir, "energy.txt")
    with open(txt, "w") as f:
        f.write("step\tE\tE_over_E0\n")
        for s, e, r in zip(steps, E, ratio):
            f.write(f"{s}\t{e:.6f}\t{r:.5f}\n")
        f.write(f"# energy drift over run = {drift:.2f}%\n")
    print(f"wrote {txt}; drift = {drift:.2f}%")

    plt = _plt()
    if plt is None:
        return
    plt.figure(figsize=(5, 3.2))
    plt.plot(steps, ratio, "o-", color=OKABE["blue"])
    plt.axhline(1.0, ls="--", color=OKABE["grey"], alpha=0.7)
    plt.fill_between([steps[0], steps[-1]], [0.99, 0.99], [1.01, 1.01],
                     color=OKABE["yellow"], alpha=0.25, label="$\\pm$1%")
    plt.xlabel("time step"); plt.ylabel("total energy / initial")
    plt.title("Energy conservation"); plt.ylim(0.99, 1.012)
    plt.legend(); plt.grid(True, alpha=0.3)
    p = os.path.join(outdir, "energy.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")


# ---------------------------------------------------------------- weak
def weak_plots(rows, outdir):
    rows = sorted(rows, key=lambda r: int(r["np"]))
    nps   = [int(r["np"]) for r in rows]
    total = [float(r["total_s"]) for r in rows]
    t1    = total[0]
    eff   = [t1 / t for t in total]      # weak efficiency T1/Tp (ideal = 1)

    txt = os.path.join(outdir, "weak_scaling.txt")
    with open(txt, "w") as f:
        f.write("np\tn\ttotal_s\tweak_efficiency\n")
        for p, r, t, e in zip(nps, rows, total, eff):
            f.write(f"{p}\t{r['n']}\t{t:.4f}\t{e:.3f}\n")
    print(f"wrote {txt}")

    plt = _plt()
    if plt is None:
        return
    plt.figure()
    plt.plot(nps, eff, "o-", color=OKABE["blue"])
    plt.axhline(1.0, ls="--", color=OKABE["grey"], alpha=0.7)
    plt.xlabel("MPI processes (N grows with p)"); plt.ylabel("weak-scaling efficiency")
    plt.ylim(0, 1.1); plt.title("Weak scaling"); plt.grid(True, alpha=0.3)
    p = os.path.join(outdir, "weak_efficiency.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")


# -------------------------------------------------------------- hybrid
def hybrid_plots(rows, outdir):
    groups = {}
    for r in rows:
        groups.setdefault(int(r["threads"]), []).append(r)
    for t, rs in groups.items():
        rs.sort(key=lambda r: int(r["np"]))

    base = None
    for t, rs in groups.items():
        for r in rs:
            if int(r["np"]) == 1 and int(r["threads"]) == 1:
                base = float(r["median_s"])

    txt = os.path.join(outdir, "hybrid_scaling.txt")
    with open(txt, "w") as f:
        f.write("threads\tnp\tn\ttotal_s\tspeedup_vs_1x1\n")
        for t in sorted(groups):
            for r in groups[t]:
                tp = float(r["median_s"])
                sp = (base / tp) if base else float("nan")
                f.write(f"{t}\t{r['np']}\t{r['n']}\t{tp:.4f}\t{sp:.3f}\n")
    print(f"wrote {txt}")

    plt = _plt()
    if plt is None:
        return
    palette = [OKABE["blue"], OKABE["vermil"], OKABE["green"], OKABE["orange"]]
    plt.figure()
    for i, t in enumerate(sorted(groups)):
        nps   = [int(r["np"]) for r in groups[t]]
        times = [float(r["median_s"]) for r in groups[t]]
        plt.plot(nps, times, "o-", color=palette[i % len(palette)], label=f"{t} threads/rank")
    plt.xlabel("MPI ranks"); plt.ylabel("wall time [s]")
    plt.title("Hybrid MPI + OpenMP scaling"); plt.legend(); plt.grid(True, alpha=0.3)
    plt.ylim(bottom=0)
    p = os.path.join(outdir, "hybrid_time.png")
    plt.savefig(p, dpi=120, bbox_inches="tight"); plt.close(); print(f"wrote {p}")


def main():
    if len(sys.argv) < 2:
        print("usage: plot.py CSV [--strong|--scaling-n|--gran|--weak|--hybrid]",
              file=sys.stderr)
        sys.exit(2)
    path = sys.argv[1]
    flags = sys.argv[2:]
    outdir = os.path.dirname(path) or "."
    rows = read_csv(path)
    if   "--scaling-n" in flags: scaling_n_plots(rows, outdir)
    elif "--gran"      in flags: granularity_plots(rows, outdir)
    elif "--energy"    in flags: energy_plots(rows, outdir)
    elif "--weak"      in flags: weak_plots(rows, outdir)
    elif "--hybrid"    in flags: hybrid_plots(rows, outdir)
    else:                        strong_plots(rows, outdir)


if __name__ == "__main__":
    main()
