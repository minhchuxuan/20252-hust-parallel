#!/usr/bin/env python3
"""make_figs.py - regenerate report figures from THIS cluster run.

Design principle (per request): communication is plotted SEPARATELY from
compute, never crammed into one dense axis. Every figure makes one point.

Reads the cluster CSVs in bench/ and writes PNGs into report/figures/.
"""
import csv, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

BENCH = os.path.join(os.path.dirname(__file__))
FIG   = os.path.join(BENCH, "..", "report", "figures")
os.makedirs(FIG, exist_ok=True)

# Okabe-Ito colourblind-safe palette
C_TOTAL = "#D55E00"   # vermillion  - total / with comm
C_COMM  = "#0072B2"   # blue        - communication
C_COMP  = "#009E73"   # green       - compute
C_IDEAL = "#999999"   # grey        - ideal / reference
C_OUT   = "#E69F00"   # orange      - outlier marker


def read(path):
    with open(os.path.join(BENCH, path)) as f:
        return list(csv.DictReader(f))


def save(fig, name):
    p = os.path.join(FIG, name)
    fig.savefig(p, dpi=130, bbox_inches="tight")
    plt.close(fig)
    print("wrote", os.path.relpath(p))


# ---------------------------------------------------------------------------
# Experiment A: runtime vs N.  Two SEPARATE panels share an x-axis:
#   (left)  wall-clock total and communication time vs N
#   (right) compute-only time vs N (network removed) -- the real algorithm cost
# ---------------------------------------------------------------------------
def fig_scaling_n():
    rows = sorted(read("scaling_n.csv"), key=lambda r: int(r["n"]))
    N     = [int(r["n"]) for r in rows]
    total = [float(r["total_s"]) for r in rows]
    comm  = [float(r["comm_s"]) for r in rows]
    comp  = [t - c for t, c in zip(total, comm)]

    fig, (axL, axR) = plt.subplots(1, 2, figsize=(10, 3.8))

    axL.plot(N, total, "o-", color=C_TOTAL, label="total (wall-clock)")
    axL.plot(N, comm,  "s--", color=C_COMM, label="communication")
    axL.set_xlabel("number of bodies $N$"); axL.set_ylabel("time [s]")
    axL.set_title("Wall-clock is dominated by communication")
    axL.legend(); axL.grid(True, alpha=0.3); axL.set_ylim(bottom=0)

    axR.plot(N, comp, "o-", color=C_COMP, label="compute only")
    axR.set_xlabel("number of bodies $N$"); axR.set_ylabel("time [s]")
    axR.set_title("Compute cost alone (network removed)")
    axR.legend(); axR.grid(True, alpha=0.3); axR.set_ylim(bottom=0)

    fig.tight_layout()
    save(fig, "scaling_n.png")


# ---------------------------------------------------------------------------
# Communication share: a dedicated bar chart making the headline point that
# comm is ~98-99% of wall-clock at every N.
# ---------------------------------------------------------------------------
def fig_comm_share():
    rows = sorted(read("scaling_n.csv"), key=lambda r: int(r["n"]))
    N     = [int(r["n"]) for r in rows]
    total = [float(r["total_s"]) for r in rows]
    comm  = [float(r["comm_s"]) for r in rows]
    share = [100.0 * c / t for c, t in zip(comm, total)]

    fig, ax = plt.subplots(figsize=(5.2, 3.6))
    x = range(len(N))
    ax.bar(x, share, color=C_COMM, width=0.6)
    for xi, s in zip(x, share):
        ax.text(xi, s - 6, f"{s:.1f}%", ha="center", color="white", fontsize=9)
    ax.set_xticks(list(x)); ax.set_xticklabels([f"{n//1000}k" for n in N])
    ax.set_xlabel("number of bodies $N$")
    ax.set_ylabel("communication share of wall-clock [%]")
    ax.set_title("Communication is 98--99% of run time")
    ax.set_ylim(0, 105); ax.grid(True, axis="y", alpha=0.3)
    fig.tight_layout()
    save(fig, "comm_share.png")


# ---------------------------------------------------------------------------
# Experiment B: per-process granularity.  Compute and communication in
# SEPARATE stacked segments per rank, plus a small inset note on idle balance.
# Because comm >> compute we use a broken/secondary view: a compute-only panel
# (so the tiny compute bars are visible) beside the full stacked bar.
# ---------------------------------------------------------------------------
def fig_granularity():
    rows = sorted(read("timing.csv"), key=lambda r: int(r["rank"]))
    ranks = [int(r["rank"]) for r in rows]
    build = [float(r["build"]) for r in rows]
    force = [float(r["force"]) for r in rows]
    integ = [float(r["integ"]) for r in rows]
    comm  = [float(r["comm"]) for r in rows]
    comp  = [b + f + g for b, f, g in zip(build, force, integ)]

    fig, (axL, axR) = plt.subplots(1, 2, figsize=(10, 3.8))

    # Left: full per-rank time, compute + communication stacked.
    axL.bar(ranks, comp, color=C_COMP, label="compute")
    axL.bar(ranks, comm, bottom=comp, color=C_COMM, label="communication")
    axL.set_xlabel("process (MPI rank)"); axL.set_ylabel("time [s]")
    axL.set_title("Each rank: communication dwarfs compute")
    axL.legend(); axL.grid(True, axis="y", alpha=0.3); axL.set_xticks(ranks)

    # Right: compute only, so the per-rank compute balance is actually visible.
    axR.bar(ranks, comp, color=C_COMP)
    axR.set_xlabel("process (MPI rank)"); axR.set_ylabel("compute time [s]")
    axR.set_title("Compute time per rank (zoomed)")
    axR.grid(True, axis="y", alpha=0.3); axR.set_xticks(ranks)
    axR.set_ylim(0, max(comp) * 1.25)

    fig.tight_layout()
    save(fig, "granularity.png")


# ---------------------------------------------------------------------------
# Experiment C: speed-up at 2N.  THREE separate panels:
#   (left)   total wall-clock vs np  (shows the network slowdown)
#   (middle) compute-only time vs np (shows real parallel scaling)
#   (right)  the two speed-up curves side by side
# ---------------------------------------------------------------------------
def fig_speedup():
    rows = sorted(read("results.csv"), key=lambda r: int(r["np"]))
    np_   = [int(r["np"]) for r in rows]
    total = [float(r["total_s"]) for r in rows]
    comm  = [float(r["comm_s"]) for r in rows]
    comp  = [t - c for t, c in zip(total, comm)]

    t1 = total[0]; c1 = comp[0]
    su_total = [t1 / t for t in total]
    su_comp  = [c1 / c for c in comp]

    # mark the np=4 congestion outlier (flagged in the run summary)
    out_idx = np_.index(4) if 4 in np_ else None

    fig, (a1, a2, a3) = plt.subplots(1, 3, figsize=(13, 3.8))

    a1.plot(np_, total, "o-", color=C_TOTAL, label="total (with comm)")
    a1.plot(np_, comm,  "s--", color=C_COMM, label="communication")
    if out_idx is not None:
        a1.scatter([np_[out_idx]], [total[out_idx]], s=160, facecolors="none",
                   edgecolors=C_OUT, linewidths=2, zorder=5, label="congestion outlier")
    a1.set_xscale("log", base=2); a1.set_xticks(np_); a1.set_xticklabels(np_)
    a1.set_xlabel("processes"); a1.set_ylabel("time [s]")
    a1.set_title("Wall-clock: network makes it slower")
    a1.legend(fontsize=8); a1.grid(True, alpha=0.3); a1.set_ylim(bottom=0)

    a2.plot(np_, comp, "o-", color=C_COMP)
    if out_idx is not None:
        a2.scatter([np_[out_idx]], [comp[out_idx]], s=160, facecolors="none",
                   edgecolors=C_OUT, linewidths=2, zorder=5)
    a2.set_xscale("log", base=2); a2.set_xticks(np_); a2.set_xticklabels(np_)
    a2.set_xlabel("processes"); a2.set_ylabel("compute time [s]")
    a2.set_title("Compute alone keeps shrinking")
    a2.grid(True, alpha=0.3); a2.set_ylim(bottom=0)

    a3.plot(np_, su_comp,  "o-", color=C_COMP,  label="compute-only speed-up")
    a3.plot(np_, su_total, "s-", color=C_TOTAL, label="end-to-end speed-up")
    a3.plot(np_, np_, ":", color=C_IDEAL, label="ideal (linear)")
    a3.set_xscale("log", base=2); a3.set_xticks(np_); a3.set_xticklabels(np_)
    a3.set_xlabel("processes"); a3.set_ylabel("speed-up $T_1/T_p$")
    a3.set_title("Speed-up: compute vs end-to-end")
    a3.legend(fontsize=8); a3.grid(True, alpha=0.3)

    fig.tight_layout()
    save(fig, "speedup.png")


if __name__ == "__main__":
    fig_scaling_n()
    fig_comm_share()
    fig_granularity()
    fig_speedup()
    print("all figures regenerated from cluster CSVs")
