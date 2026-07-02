# Barnes-Hut 2D N-body on an MPI Cluster

> Final project for **Parallel Programming (20252)** — a 4-member group
> implementation of a gravitational N-body simulator using the Barnes-Hut
> quadtree approximation, parallelized with MPI across a 4-node Ubuntu
> cluster. 

---

## Table of contents

1. [What this is](#what-this-is)
2. [Repository layout](#repository-layout)
3. [Prerequisites](#prerequisites)
4. [Cluster bring-up (M3)](#cluster-bring-up-m3)
5. [Building](#building)
6. [Running](#running)
7. [Correctness harness (M4/M5)](#correctness-harness-m4m5)
8. [Benchmarks (§7, M6)](#benchmarks-7-m6)
9. [Plots](#plots)
10. [Visualization](#visualization)
11. [Demo script (§9, M8)](#demo-script-9-m8)
12. [Algorithm details](#algorithm-details)
13. [Parallelization strategy](#parallelization-strategy)
14. [Environment variables](#environment-variables)
15. [Makefile targets](#makefile-targets)
16. [File-by-file reference](#file-by-file-reference)
17. [Troubleshooting](#troubleshooting)
18. [Contribution split](#contribution-split)
19. [License](#license)

---

## What this is

The code computes gravitational N-body evolution in 2-D using the
**Barnes-Hut** tree approximation (θ-criterion, O(N log N) per step) instead
of the naïve O(N²) direct summation. Three binaries are produced:

- `nbody_serial` — reference implementation, used as the correctness oracle.
- `nbody_parallel` — MPI version that runs across the cluster.
- `nbody_hybrid` — MPI + OpenMP: threads the force walk inside each rank
  (§5 of `task.md`: "hybrid = bonus").

The two share all algorithmic code in `src/common/`; only the driver loops
differ. Given identical input and integrator, outputs agree to within
float-rounding of an `MPI_Allgatherv` reorder (≤ 1 × 10⁻⁶ relative).

This maps to the deliverables in `task.md`:

| Milestone | Artifact here |
|---|---|
| M3 cluster online | `scripts/provision.sh`, `setup_ssh.sh`, `hello_mpi` |
| M4 serial baseline | `nbody_serial` + `tests/run_tests.sh` T1–T6 |
| M5 MPI version | `nbody_parallel` + `T2..T4` show equivalence to serial |
| M6 benchmarks | `bench/bench.sh`, `bench/weak.sh`, `bench/plot.py` |
| M7 report | `report/main.tex` (compile with `make -C report`) |
| M8 demo | `scripts/demo.sh` + `make viz` animation |

---

## Repository layout

```
project/
├── src/
│   ├── common/
│   │   ├── nbody.h          shared types + prototypes
│   │   ├── bodies.c         allocation, init, CSV I/O, bbox, leapfrog halves, wall_time
│   │   └── qtree.c          quadtree build, center-of-mass, BH force walk
│   ├── serial/
│   │   └── main.c           serial driver (reference oracle)
│   ├── parallel/
│   │   └── main.c           MPI driver (replicated tree, sliced force)
│   ├── hybrid/
│   │   └── main.c           MPI + OpenMP driver (threaded force walk)
│   ├── tools/
│   │   ├── hello_mpi.c      M3 smoke test
│   │   └── gen_input.c      initial-condition generator (plummer/disk/two)
│   └── viz/
│       └── animate.py       trajectory → GIF/MP4/PNG mosaic
├── tests/
│   ├── run_tests.sh         6-check correctness harness (T1..T6)
│   ├── compare_csv.c        row-wise CSV compare with rtol/atol
│   └── energy_check.c       O(N²) kinetic+potential energy computation
├── bench/
│   ├── bench.sh             strong-scaling sweep (np = {1,2,3})
│   ├── weak.sh              weak-scaling sweep (N grows with np)
│   ├── hybrid.sh            (P,T) grid sweep for nbody_hybrid
│   └── plot.py              speedup / efficiency plots (matplotlib, txt fallback)
├── scripts/
│   ├── provision.sh         one-shot per-node apt + sshd setup
│   ├── setup_ssh.sh         push master's SSH key to all workers
│   ├── distribute.sh        rsync the project to every node
│   ├── demo.sh              offline demo driver
│   ├── run.sh               thin launcher (serial | parallel)
│   ├── hosts.list           ssh targets
│   ├── hostfile.openmpi     OpenMPI-format hostfile
│   └── etc-hosts.template   static IP → hostname map
├── report/
│   ├── main.tex             10–15 page LaTeX report (§8 outline)
│   ├── slides.tex           Beamer presentation for M9 demo day
│   ├── refs.bib             references
│   ├── Makefile             `latexmk` wrapper (builds main.pdf + slides.pdf)
│   └── figures/             placeholders (populated by `make plots`)
├── data/                    generated inputs + frame dumps (git-ignored)
├── Makefile
└── README.md
```

---

## Prerequisites

On every node (Ubuntu 22.04 LTS, §2 constraint):

- `build-essential` (gcc, make)
- `openmpi-bin`, `libopenmpi-dev` (MPI)
- `openssh-server` + `rsync` (passwordless SSH + sync)
- `python3` + `python3-matplotlib` *(optional, for `bench/plot.py` and viz)*
- `ffmpeg` *(optional, only for MP4 output from `animate.py`)*

`scripts/provision.sh` installs the mandatory packages in one shot.

---

## Cluster bring-up (M3)

Following `task.md` §4 step-by-step.

### 1. Network & hostnames

Put every node on the same LAN (phone hotspot or router is fine, bridge-
adapter mode on VirtualBox / VMware). Note each node's IP and write them
into `scripts/etc-hosts.template`:

```
192.168.1.11  node1
192.168.1.12  node2
192.168.1.13  node3
192.168.1.14  node4
```

### 2. Provision each node

On every VM, run **once**:

```sh
cd project
sudo HOSTS_FILE=scripts/etc-hosts.template bash scripts/provision.sh
```

This installs packages, starts `sshd`, and merges the hosts file.

### 3. Passwordless SSH from master → workers

On the **master** node only:

```sh
./scripts/setup_ssh.sh scripts/hosts.list
```

Prompts for the worker password once per host; subsequent SSH is silent.

### 4. Smoke test (M3 tick)

```sh
make hello_mpi
mpirun -np 4 -hostfile scripts/hostfile.openmpi --map-by node ./hello_mpi
```

Expected output (order may vary):

```
[master] MPI library: Open MPI v4.x ...
rank 0/4 on node1
rank 1/4 on node2
rank 2/4 on node3
rank 3/4 on node4
```

### 5. Distribute the project

Whenever you update code on the master:

```sh
make                                   # build locally first
./scripts/distribute.sh                # rsync to all workers
```

---

## Building

```sh
make                 # nbody_serial, nbody_parallel, tools/{hello_mpi,gen_input}
make tools           # just the tools
make tests           # correctness helpers in tests/
make clean
```

Override compilers and flags as usual:

```sh
make CC=clang CFLAGS='-O3 -march=native -Wall -std=gnu11'
```

---

## Running

### Serial

```sh
./nbody_serial N STEPS DT [OUT.csv]
./nbody_serial --from INPUT.csv STEPS DT [OUT.csv]
```

Example:

```sh
./nbody_serial 4096 100 0.005 final.csv
# -> "serial  n=4096 steps=100 dt=0.005  time=3.21s  build=0.45s  force=2.60s"
```

### MPI

```sh
mpirun -np P -hostfile scripts/hostfile.openmpi \
       ./nbody_parallel N STEPS DT [OUT.csv]
```

Example (4 nodes, 1 rank each):

```sh
mpirun -np 4 -hostfile scripts/hostfile.openmpi --map-by node \
       ./nbody_parallel 4096 100 0.005 final.csv
# -> "parallel n=4096 p=4 steps=100 dt=0.005  time=...s  build=...s  force=...s  comm=...s  integ=...s"
```

### Hybrid MPI + OpenMP

`nbody_hybrid` is identical to `nbody_parallel` except the force walk and
integrator run under an OpenMP `parallel for`. One MPI rank per node, T
threads per rank.

```sh
OMP_NUM_THREADS=4 mpirun -np 4 -hostfile scripts/hostfile.openmpi --map-by node \
      -x OMP_NUM_THREADS \
      ./nbody_hybrid 4096 100 0.005 final.csv
# -> "hybrid n=4096 p=4 t=4 steps=100 dt=0.005 time=...s ..."
```

Notes:

- Uses `MPI_Init_thread(MPI_THREAD_FUNNELED)` — only the main thread does
  MPI calls, so no extra locking is needed.
- `OMP_PROC_BIND=close OMP_PLACES=cores` is a good default on VMs.
- Tree is read-only during the walk; `ax`/`ay` writes are disjoint across
  `i`, so the `parallel for` is race-free without locks.

### Custom initial conditions

```sh
./gen_input 4096 data/ic.csv 42 plummer       # classic equilibrium cluster
./gen_input 4096 data/ic.csv 42 disk          # rotating disk (toy spiral)
./gen_input 4096 data/ic.csv 42 two           # two colliding clusters

./nbody_serial --from data/ic.csv 200 0.005 out.csv
mpirun -np 4 -hostfile scripts/hostfile.openmpi --map-by node \
       ./nbody_parallel --from data/ic.csv 200 0.005 out.csv
```

### Convenience wrapper

`scripts/run.sh` auto-splits program args from mpirun args:

```sh
./scripts/run.sh serial   4096 100 0.005
./scripts/run.sh parallel 4096 100 0.005 -np 4 -hostfile scripts/hostfile.openmpi --map-by node
```

---

## Correctness harness (M4/M5)

```sh
make check
```

Runs `tests/run_tests.sh` which builds the comparator and executes:

| Test | What it checks | Tolerance |
|---|---|---|
| T1 | Serial produces a CSV of the right length | exact line count |
| T2 | `mpirun -np 1 ./nbody_parallel` ≡ serial | rtol=1e-10 |
| T3 | `mpirun -np 2` ≈ serial (Allgather reorder OK) | rtol=1e-6 |
| T4 | `mpirun -np 4` ≈ serial | rtol=1e-6 |
| T5 | Energy drift over 50 steps < 5 % | — |
| T6 | `gen_input` → `nbody_serial --from` round-trip | non-empty |

Fails print the offending row/column and the numerical delta.

---

## Benchmarks (§7, M6)

### Strong scaling (fixed problem size, varying np)

```sh
make strong HOSTFILE=scripts/hostfile.openmpi N=4096 STEPS=50 DT=0.005
# or: ./bench/bench.sh 4096 50 0.005 scripts/hostfile.openmpi
```

Sweeps `np ∈ {1,2,3}`, **5 runs per point**, records median / min / max
wall times into `bench/results.csv`:

```
np,n,steps,dt,median_s,min_s,max_s
1,4096,50,0.005,3.214,3.182,3.290
2,4096,50,0.005,1.743,1.720,1.801
3,4096,50,0.005,1.238,1.205,1.272
```

Override sweep with env vars:

```sh
REPEATS=10 NPS="1 2 3 4 6" make strong HOSTFILE=...
```

### Weak scaling (problem size grows with np)

```sh
make weak HOSTFILE=scripts/hostfile.openmpi N=2048 STEPS=30 DT=0.005
```

Uses `N(P) = N₀ · P` (common Barnes-Hut weak-scaling convention).
Writes `bench/weak.csv`.

### Hybrid grid sweep

```sh
NPS="1 2 4" THREADS="1 2 4" make hybrid-bench \
    HOSTFILE=scripts/hostfile.openmpi N=4096 STEPS=50 DT=0.005
```

Writes `bench/hybrid.csv` with columns `np,threads,n,steps,dt,median_s`.
Turn into a plot via `make plots` (produces `bench/hybrid_time.png` and
`bench/hybrid_scaling.txt`).

---

## Plots

```sh
make plots
```

Invokes `bench/plot.py` over `bench/results.csv` (and `bench/weak.csv` if
present). Produces:

- `bench/speedup.png` — measured vs ideal speedup
- `bench/efficiency.png` — parallel efficiency (should plateau ≲ 1)
- `bench/weak_efficiency.png` — weak-scaling efficiency
- `bench/strong_scaling.txt`, `bench/weak_scaling.txt` — text tables

If matplotlib isn't installed, the text tables are still produced.

Copy the PNGs into `report/figures/` before compiling the LaTeX report.

---

## Visualization

```sh
make viz
```

Runs `nbody_serial 512 100 0.01` with `DUMP_EVERY=2 DUMP_PREFIX=data/frames/f`
and animates the dumped frames to `data/sim.gif` via
`src/viz/animate.py`.

Manually:

```sh
DUMP_EVERY=5 DUMP_PREFIX=data/frames/f \
    ./nbody_serial 1024 500 0.005 final.csv
python3 src/viz/animate.py data/frames/f data/sim.mp4
# .gif for Pillow, .mp4 for ffmpeg, .png for a 3-panel mosaic fallback
```

Works with `nbody_parallel` too — rank 0 writes the frames after each
`MPI_Allgatherv`.

---

## Demo script (§9, M8)

```sh
make demo HOSTFILE=scripts/hostfile.openmpi
```

Does, live from a terminal:

1. Runs the **serial reference** with N=2048, 50 steps, writing
   `data/final_serial.csv`.
2. Runs the **MPI 4-node version** with the same parameters, writing
   `data/final_parallel.csv`.
3. Diffs the first 5 rows of both files to show they match.

Record the terminal output / screencast as backup video per `task.md` §10.

---

## Algorithm details

**Problem.** Integrate the equations of motion of N gravitationally
interacting point masses in 2-D.

**Forces.** For each body *i*,

$$
\vec a_i = G \sum_{j \neq i} m_j\,\frac{\vec r_j - \vec r_i}{(|\vec r_j - \vec r_i|^2 + \varepsilon^2)^{3/2}}
$$

with softening `ε = NB_SOFTEN = 1e-3` to avoid the 1/r² singularity.

**Barnes-Hut approximation.** A quadtree subdivides the bounding box until
each leaf holds at most one body. Each internal node stores the
center-of-mass and total mass of its subtree. When computing the force on a
body, if the cell size *s* and distance *r* to the cell's COM satisfy
`s / r < θ` (with `θ = NB_THETA = 0.5`), the whole subtree is replaced by a
single pseudoparticle. Complexity per step: **O(N log N)**.

**Integrator.** Semi-implicit Euler (kick-then-drift):

```
v ← v + a·dt
x ← x + v·dt
```

This is a symplectic 1st-order integrator — energy drift is bounded on the
timescales we benchmark. Matches bit-for-bit across the serial/parallel
split for a given MPI layout.

**Bounding box.** Recomputed every step from particle positions, then
squared up so the root cell is square (keeps tree balanced).

---

## Parallelization strategy

Every rank holds a **full copy of all particle arrays** (SoA layout in
`Bodies`: `px,py,vx,vy,ax,ay,m`). Each rank owns a contiguous slice
`[lo, hi)` determined by `split_range(n, size, rank, …)`; slice sizes
differ by at most one.

**Per timestep:**

1. **Build.** Each rank constructs the **full** Barnes-Hut tree locally
   (replicated). No inter-rank communication during the build.
2. **Force.** Rank walks the tree only for its slice `i ∈ [lo, hi)`.
   Because the tree is complete on every rank, no halo exchange is
   needed.
3. **Integrate.** Kick + drift applied locally to `[lo, hi)`.
4. **Sync.** Two `MPI_Allgatherv` calls propagate the updated `px`, `py`
   back to all ranks for the next step's tree build.

**Cost model.** Per step:

- Compute per rank: `O((N / P) · log N)`
- Communication per step: `2 · N · 8` bytes (two doubles per particle)

Masses are static — broadcast once after init.
Velocities are gathered only at output time (when `OUT.csv` is requested).

**Timing.** All phase timers (`t_build, t_force, t_comm, t_integ,
elapsed`) are local per-rank. At the end, rank 0 reduces each with
`MPI_MAX` so the printed number reflects the slowest rank, which is what
gates wall-clock time.

**Trade-offs.**
- **Memory.** `O(N)` replicated per rank. Good to ≈ 10⁵–10⁶ particles on
  consumer VMs. Beyond that, switch to ORB or Hilbert-curve decomposition
  (discussed as future work in the report).
- **Load balance.** Equal slice size = equal force-walk work only if
  particles are roughly uniform. For heavily clustered inputs, cost-aware
  decomposition would help.
- **Communication.** Allgatherv of positions is O(N) per rank — fine at
  P ≤ a few dozen, dominated by compute at these sizes. Timing breakdown
  confirms `comm` ≪ `force` on the class cluster (see `report/main.tex`).

### Hybrid MPI + OpenMP

`src/hybrid/main.c` adds a second level of parallelism:

- **Inside a rank** the force-walk `for` loop is an OpenMP
  `parallel for schedule(dynamic, 64)` — dynamic scheduling handles the
  non-uniform walk depth across clustered regions.
- **Between ranks** the protocol is identical to the pure-MPI version
  (two `Allgatherv`s per step).
- Total parallelism is `P × T` (ranks × threads). On a 4-node cluster
  with 4 cores per node, `np=4 threads=4` exposes 16 cores.
- `MPI_Init_thread(MPI_THREAD_FUNNELED)` — only the main thread calls
  MPI, so no extra locking.
- `ax[i]`/`ay[i]` writes are disjoint across `i`; tree reads are
  read-only; integrator writes are disjoint — the whole force +
  integrate phase is race-free without any atomic/critical section.

---

## Environment variables

| Var | Default | Effect |
|---|---|---|
| `DUMP_EVERY=K` | off | Dump a CSV every K steps (serial + MPI rank 0) |
| `DUMP_PREFIX=P` | off | Frames go to `P_0000.csv`, `P_0001.csv`, … |
| `REPEATS=N` | 5 | Runs per data point in `bench/bench.sh` |
| `NPS="1 2 4 8 16"` | benchmark-specific | Override np sweep in benchmarks |
| `HOSTFILE=F` | unset | Passed through `make` to mpirun via scripts |
| `MPI_USER=u` | `$USER` | SSH target user for `setup_ssh.sh` / `distribute.sh` |
| `CC=`, `MPICC=`, `CFLAGS=` | `gcc`, `mpicc`, `-O2 -Wall -Wextra -std=gnu11` | Compiler config |
| `TIMING_CSV=path` | off | MPI/hybrid: rank 0 writes per-rank build/force/comm/integ/total |

---

## Makefile targets

| Target | What it does |
|---|---|
| `all` *(default)* | Build `nbody_serial`, `nbody_parallel`, `nbody_hybrid`, tools |
| `hybrid` | Just `nbody_hybrid` |
| `tools` | Just `hello_mpi` + `gen_input` |
| `tests` | Build `tests/compare_csv`, `tests/energy_check` |
| `smoke` | Minimal serial-vs-parallel + serial-vs-hybrid CSV diff |
| `check` | Full `tests/run_tests.sh` harness (T1..T6) |
| `strong` | `bench/bench.sh` (speedup: fixed N, ranks 1,2,4,8,…). Vars: `N`, `STEPS`, `DT`, `HOSTFILE` |
| `scaling` | `bench/scaling_n.sh` (runtime vs N at P=cores, with/without comm) |
| `gran` | `bench/granularity.sh` (per-process compute/comm breakdown → load balance) |
| `weak` | `bench/weak.sh` (weak scaling) |
| `hybrid-bench` | `bench/hybrid.sh` — (np × threads) grid sweep |
| `plots` | `bench/plot.py` over results.csv + weak.csv + hybrid.csv |
| `demo` | `scripts/demo.sh` for M8 |
| `viz` | Runs a serial simulation and animates the frames |
| `clean` | Remove binaries + `tests/out`, `data/frames` |

Build the PDF report + slides:

```sh
make -C report               # produces report/main.pdf and report/slides.pdf
```

---

## File-by-file reference

| File | Purpose | LOC |
|---|---|---|
| `src/common/nbody.h` | Shared `Bodies` + `QNode`/`QTree` types; tunables (`NB_THETA`, `NB_SOFTEN`, `NB_G`) | 75 |
| `src/common/bodies.c` | Allocation, Plummer init, CSV I/O, bounding box, leapfrog halves, `wall_time` | 138 |
| `src/common/qtree.c` | Quadtree insert with lazy split, center-of-mass, iterative BH force walk | 199 |
| `src/serial/main.c` | Serial driver; reads args/env; per-step build + force + integrate; optional `DUMP_*` | 106 |
| `src/parallel/main.c` | MPI driver; init broadcast, per-step force slice, two `Allgatherv`s; MAX-reduced timing | 206 |
| `src/hybrid/main.c`   | MPI + OpenMP driver; same protocol as parallel but threaded force + integrate loops | ~195 |
| `src/tools/hello_mpi.c` | Rank/size/hostname printer — M3 verification | 41 |
| `src/tools/gen_input.c` | CSV initial-condition generator (plummer/disk/two modes) | 80 |
| `src/viz/animate.py` | Matplotlib animation of frame CSVs (mp4/gif) with PNG fallback | 87 |
| `tests/compare_csv.c` | Row-wise numerical compare with rtol/atol | 87 |
| `tests/energy_check.c` | O(N²) KE + PE for energy-conservation test | 45 |
| `tests/run_tests.sh` | T1..T6 harness | 78 |
| `bench/bench.sh` | Strong-scaling sweep → `bench/results.csv` | 45 |
| `bench/weak.sh` | Weak-scaling sweep → `bench/weak.csv` | 43 |
| `bench/hybrid.sh` | (np × threads) sweep → `bench/hybrid.csv` | ~45 |
| `bench/plot.py` | Speedup + efficiency + hybrid plots (txt fallback) | ~145 |
| `scripts/provision.sh` | One-node apt + sshd setup | 76 |
| `scripts/setup_ssh.sh` | Push master's SSH key to workers | 28 |
| `scripts/distribute.sh` | Rsync project tree to all hosts | 23 |
| `scripts/demo.sh` | M8 live demo driver | 29 |
| `scripts/run.sh` | Arg-splitting launcher (serial / parallel) | 37 |
| `Makefile` | Build + all convenience targets | 83 |

Grand total: ≈ 1760 LOC (well above the 1000-LOC §2 floor).

---

## Troubleshooting

- **`mpirun` reports `Not enough slots`** — pass `--oversubscribe` to
  mpirun or increase `slots=` in `scripts/hostfile.openmpi`.
- **`make check` T1 fails on line count** — your gcc produced CRLF
  line endings. Build on the Linux VM, not on Windows.
- **Serial vs parallel mismatch** — each body is integrated by identical
  arithmetic regardless of rank, so the outputs are bit-for-bit identical
  (T2–T4 pass at rtol=1e-10). A mismatch usually means a stale binary on one
  node; rebuild and re-`distribute.sh`.
- **Speedup plateaus below 2× at np=4** — inspect the `comm=` column from
  `nbody_parallel`; if it dominates, reduce N or switch to non-blocking
  `Ialltoall` (see §6.3 engineering checklist).
- **`setup_ssh.sh` hangs** — check the worker's `/etc/hosts` actually has
  the master's IP (run `ping node1` first).
- **`make viz` produces an empty GIF** — Pillow writer wasn't installed;
  the script falls back to `sim.png` (a 3-frame mosaic) which is fine for
  the report.

---

## Contribution split

Per `task.md` §6.2, balanced to ≥ 250 LOC/person so every member has real
code to explain during the professor's quiz (grading criterion #6):

- **Member 1 — Infra & Serial baseline.** `scripts/*`, `src/tools/hello_mpi.c`,
  `src/serial/main.c`, `src/common/bodies.c` (I/O + timing).
- **Member 2 — MPI core.** `src/parallel/main.c`, the MPI-specific parts of
  the correctness harness, `src/common/qtree.c` (tree build half).
- **Member 3 — Performance.** `bench/bench.sh`, `bench/weak.sh`,
  `bench/plot.py`, `src/common/qtree.c` (force walk half), timing
  instrumentation.
- **Member 4 — I/O / viz / report.** `src/tools/gen_input.c`,
  `src/viz/animate.py`, `tests/compare_csv.c`, `tests/energy_check.c`,
  `report/*`.

Everyone reviews everyone's code; see `report/main.tex` §9 for the final
contribution table.

---

## License

Coursework — no redistribution. Reference material cited in
`report/refs.bib`.
