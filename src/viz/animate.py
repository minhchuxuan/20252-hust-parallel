#!/usr/bin/env python3
"""src/viz/animate.py - render an MP4/GIF animation from trajectory frames.

Produce frames first:
    DUMP_EVERY=1 DUMP_PREFIX=data/frames/f \\
        ./nbody_serial 1024 200 0.005

Then:
    python3 src/viz/animate.py data/frames/f data/sim.mp4

Falls back to a PNG mosaic of the first/mid/last frame if matplotlib's
animation backend isn't available (e.g. no ffmpeg).
"""
import sys, glob, os, csv

def load(path):
    xs, ys = [], []
    with open(path) as f:
        r = csv.reader(f); next(r)
        for row in r:
            xs.append(float(row[1])); ys.append(float(row[2]))
    return xs, ys

def main():
    if len(sys.argv) < 3:
        print("usage: animate.py PREFIX OUT.{mp4,gif,png}", file=sys.stderr)
        sys.exit(2)
    prefix, out = sys.argv[1], sys.argv[2]
    frames = sorted(glob.glob(f"{prefix}_*.csv"))
    if not frames:
        print(f"no frames found at {prefix}_*.csv", file=sys.stderr); sys.exit(1)

    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    # A robust view window: use a central percentile of the first frame so a few
    # high-velocity escapers do not shrink the bound cluster to a dot.
    def central_limits(xs, ys, frac=0.97):
        sx, sy = sorted(xs), sorted(ys)
        lo, hi = (1 - frac) / 2, (1 + frac) / 2
        n = len(sx)
        xl, xh = sx[int(lo * n)], sx[min(n - 1, int(hi * n))]
        yl, yh = sy[int(lo * n)], sy[min(n - 1, int(hi * n))]
        span = max(xh - xl, yh - yl, 1e-9)
        cx, cy = 0.5 * (xl + xh), 0.5 * (yl + yh)
        h = 0.6 * span
        return (cx - h, cx + h), (cy - h, cy + h)

    xs0, ys0 = load(frames[0])
    xlim, ylim = central_limits(xs0, ys0)

    ext = os.path.splitext(out)[1].lower()
    if ext == ".png":
        # mosaic: start / middle / end
        picks = [frames[0], frames[len(frames)//2], frames[-1]]
        labels = ["start", "middle", "end"]
        fig, axs = plt.subplots(1, 3, figsize=(12, 4))
        for ax, fp, lab in zip(axs, picks, labels):
            xs, ys = load(fp)
            ax.scatter(xs, ys, s=1)
            ax.set_xlim(*xlim); ax.set_ylim(*ylim); ax.set_aspect("equal")
            ax.set_title(lab)
        fig.tight_layout(); fig.savefig(out, dpi=120); plt.close(fig)
        print(f"wrote {out}"); return

    from matplotlib.animation import FuncAnimation, PillowWriter, FFMpegWriter
    fig, ax = plt.subplots(figsize=(6,6))
    sc = ax.scatter([], [], s=1)
    ax.set_xlim(*xlim); ax.set_ylim(*ylim); ax.set_aspect("equal")
    title = ax.set_title("")

    def upd(i):
        xs, ys = load(frames[i])
        sc.set_offsets(list(zip(xs, ys)))
        title.set_text(f"frame {i+1}/{len(frames)}")
        return sc, title

    anim = FuncAnimation(fig, upd, frames=len(frames), blit=False, interval=40)
    try:
        if ext == ".gif":
            anim.save(out, writer=PillowWriter(fps=25))
        else:
            anim.save(out, writer=FFMpegWriter(fps=25, bitrate=2000))
        print(f"wrote {out}")
    except Exception as e:
        fallback = os.path.splitext(out)[0] + ".png"
        print(f"animation writer failed ({e}); falling back to {fallback}", file=sys.stderr)
        sys.argv[2] = fallback
        plt.close(fig)
        main()

if __name__ == "__main__":
    main()
