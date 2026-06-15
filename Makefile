# Makefile - builds serial + MPI + hybrid Barnes-Hut binaries, tools, tests.
#
# Targets:
#   make              -> nbody_serial, nbody_parallel, nbody_hybrid, tools
#   make hybrid       -> just nbody_hybrid
#   make tools        -> gen_input, hello_mpi
#   make tests        -> compare_csv, energy_check
#   make smoke        -> quick serial-vs-parallel CSV diff
#   make check        -> full tests/run_tests.sh harness
#   make strong       -> bench/bench.sh (strong scaling)
#   make weak         -> bench/weak.sh  (weak scaling)
#   make hybrid-bench -> bench/hybrid.sh (sweeps (P,T) grid)
#   make plots        -> bench/plot.py over results.csv (+ weak if present)
#   make demo         -> scripts/demo.sh
#   make viz          -> dump frames with serial run, animate to data/sim.gif
#   make clean

CC      ?= gcc
MPICC   ?= mpicc
# -std=gnu11 (not c11): the code uses POSIX clock_gettime and the X/Open M_PI,
# which a strict ISO -std=c11 hides. The sources also carry feature-test-macro
# fallbacks, so they still build if CFLAGS is overridden with -std=c11.
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
OMPFLAGS ?= -fopenmp
LDLIBS  ?= -lm

COMMON_SRC = src/common/bodies.c src/common/qtree.c
COMMON_HDR = src/common/nbody.h

SERIAL_SRC   = src/serial/main.c   $(COMMON_SRC)
PARALLEL_SRC = src/parallel/main.c $(COMMON_SRC)
HYBRID_SRC   = src/hybrid/main.c   $(COMMON_SRC)

all: nbody_serial nbody_parallel nbody_hybrid tools

tools: gen_input hello_mpi

tests: compare_csv energy_check

hybrid: nbody_hybrid

nbody_serial: $(SERIAL_SRC) $(COMMON_HDR)
	$(CC) $(CFLAGS) -o $@ $(SERIAL_SRC) $(LDLIBS)

nbody_parallel: $(PARALLEL_SRC) $(COMMON_HDR)
	$(MPICC) $(CFLAGS) -o $@ $(PARALLEL_SRC) $(LDLIBS)

nbody_hybrid: $(HYBRID_SRC) $(COMMON_HDR)
	$(MPICC) $(CFLAGS) $(OMPFLAGS) -o $@ $(HYBRID_SRC) $(LDLIBS)

gen_input: src/tools/gen_input.c $(COMMON_SRC) $(COMMON_HDR)
	$(CC) $(CFLAGS) -o $@ src/tools/gen_input.c $(COMMON_SRC) $(LDLIBS)

hello_mpi: src/tools/hello_mpi.c
	$(MPICC) $(CFLAGS) -o $@ src/tools/hello_mpi.c

compare_csv: tests/compare_csv.c
	$(CC) $(CFLAGS) -o tests/compare_csv tests/compare_csv.c $(LDLIBS)

energy_check: tests/energy_check.c $(COMMON_SRC) $(COMMON_HDR)
	$(CC) $(CFLAGS) -o tests/energy_check tests/energy_check.c $(COMMON_SRC) $(LDLIBS)

smoke: all tests
	./nbody_serial 256 10 0.01 /tmp/nb_s.csv
	mpirun -np 2 --oversubscribe ./nbody_parallel 256 10 0.01 /tmp/nb_p.csv
	./tests/compare_csv /tmp/nb_s.csv /tmp/nb_p.csv 1e-6 1e-9
	OMP_NUM_THREADS=2 mpirun -np 2 --oversubscribe ./nbody_hybrid 256 10 0.01 /tmp/nb_h.csv
	./tests/compare_csv /tmp/nb_s.csv /tmp/nb_h.csv 1e-6 1e-9

check: all tests
	./tests/run_tests.sh

strong: all
	./bench/bench.sh $(N) $(STEPS) $(DT) $(HOSTFILE)

scaling: all
	./bench/scaling_n.sh

gran: all
	./bench/granularity.sh $(N) $(STEPS) $(DT) $(HOSTFILE)

energy: all tests
	./bench/energy.sh

weak: all
	./bench/weak.sh $(N) $(STEPS) $(DT) $(HOSTFILE)

hybrid-bench: nbody_hybrid
	./bench/hybrid.sh $(N) $(STEPS) $(DT) $(HOSTFILE)

plots:
	@[ -f bench/results.csv ]   && python3 bench/plot.py bench/results.csv   --strong    || true
	@[ -f bench/scaling_n.csv ] && python3 bench/plot.py bench/scaling_n.csv --scaling-n || true
	@[ -f bench/timing.csv ]    && python3 bench/plot.py bench/timing.csv    --gran      || true
	@[ -f bench/weak.csv ]      && python3 bench/plot.py bench/weak.csv      --weak      || true
	@[ -f bench/hybrid.csv ]    && python3 bench/plot.py bench/hybrid.csv    --hybrid    || true

demo: all tests
	HOSTFILE=$(HOSTFILE) ./scripts/demo.sh

viz: all
	mkdir -p data/frames
	DUMP_EVERY=2 DUMP_PREFIX=data/frames/f \
		./nbody_serial 512 100 0.01 data/final.csv
	python3 src/viz/animate.py data/frames/f data/sim.gif

clean:
	rm -f nbody_serial nbody_parallel nbody_hybrid gen_input hello_mpi \
	      tests/compare_csv tests/energy_check
	rm -rf tests/out data/frames

.PHONY: all hybrid tools tests smoke check strong scaling gran energy weak hybrid-bench plots demo viz clean
