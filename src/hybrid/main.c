/* hybrid/main.c - Hybrid MPI + OpenMP Barnes-Hut driver.
 *
 * Identical to src/parallel/main.c except the force loop is parallelized
 * with OpenMP inside each MPI rank. The tree is read-only during the
 * walk, so no synchronization is needed beyond the implicit parallel-for
 * barrier.
 *
 * Build (handled by the top-level Makefile):
 *   mpicc -O2 -fopenmp -o nbody_hybrid ... -lm
 *
 * Run:
 *   OMP_NUM_THREADS=2 mpirun -np 3 -hostfile hosts ./nbody_hybrid N STEPS DT [OUT.csv]
 *
 * Report notes:
 *   Two-level decomposition: MPI across nodes (one rank/node), OpenMP
 *   across cores within a node. Total parallelism P*T. Allgatherv comm
 *   cost is unchanged (still 2*N doubles/step) but the per-rank force
 *   phase shrinks by a further factor ~T.
 */
#include "../common/nbody.h"
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _OPENMP
#  include <omp.h>
#endif

static void split_range(int n, int p, int r, int *lo, int *hi) {
    int base = n / p;
    int rem  = n % p;
    *lo = r * base + (r < rem ? r : rem);
    *hi = *lo + base + (r < rem ? 1 : 0);
}

int main(int argc, char **argv) {
    /* MPI_THREAD_FUNNELED is enough: only the main thread calls MPI. */
    int provided = MPI_THREAD_SINGLE;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    if (provided < MPI_THREAD_FUNNELED) {
        fprintf(stderr, "MPI implementation lacks MPI_THREAD_FUNNELED\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc < 4) {
        if (rank == 0) fprintf(stderr,
            "usage: mpirun -np P %s N STEPS DT [OUT.csv]\n"
            "       mpirun -np P %s --from FILE STEPS DT [OUT.csv]\n"
            "env:   OMP_NUM_THREADS=T\n",
            argv[0], argv[0]);
        MPI_Finalize(); return 1;
    }

    int n = 0, steps = 0;
    double dt = 0.0;
    const char *out = NULL;
    const char *from = NULL;

    if (strcmp(argv[1], "--from") == 0) {
        from  = argv[2];
        steps = atoi(argv[3]);
        dt    = atof(argv[4]);
        if (argc >= 6) out = argv[5];
        if (rank == 0) {
            n = bodies_count_csv(from);
            if (n <= 0) { fprintf(stderr, "empty or unreadable input: %s\n", from);
                          MPI_Abort(MPI_COMM_WORLD, 1); }
        }
        MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);
    } else {
        n     = atoi(argv[1]);
        steps = atoi(argv[2]);
        dt    = atof(argv[3]);
        if (argc >= 5) out = argv[4];
    }
    if (n <= 0) { MPI_Abort(MPI_COMM_WORLD, 1); }

    Bodies b = bodies_alloc(n);

    /* Per-rank slice */
    int lo, hi;
    split_range(n, size, rank, &lo, &hi);
    int *counts = (int*)malloc(size * sizeof(int));
    int *displs = (int*)malloc(size * sizeof(int));
    for (int r = 0; r < size; r++) {
        int L, H; split_range(n, size, r, &L, &H);
        counts[r] = H - L;
        displs[r] = L;
    }

    /* Optional trajectory dump */
    int dump_every = 0;
    const char *dump_prefix = NULL;
    if (rank == 0) {
        const char *de = getenv("DUMP_EVERY");
        const char *dp = getenv("DUMP_PREFIX");
        if (de && dp) { dump_every = atoi(de); dump_prefix = dp; }
    }
    MPI_Bcast(&dump_every, 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        if (from) bodies_read_csv(&b, from);
        else      bodies_init_plummer(&b, 42u);
    }
    MPI_Bcast(b.px, n, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(b.py, n, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(b.vx, n, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(b.vy, n, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    MPI_Bcast(b.m,  n, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    int threads = 1;
#ifdef _OPENMP
    #pragma omp parallel
    {
        #pragma omp single
        threads = omp_get_num_threads();
    }
#endif

    double t_comm = 0.0, t_build = 0.0, t_force = 0.0, t_integ = 0.0;
    MPI_Barrier(MPI_COMM_WORLD);
    double t0 = MPI_Wtime();

    if (dump_every > 0 && rank == 0 && dump_prefix) {
        char path[512];
        snprintf(path, sizeof(path), "%s_%04d.csv", dump_prefix, 0);
        bodies_write_csv(&b, path);
    }

    for (int s = 0; s < steps; s++) {
        double xmin, ymin, xmax, ymax;
        bodies_bbox(&b, &xmin, &ymin, &xmax, &ymax);

        double tb0 = MPI_Wtime();
        QTree t;
        qtree_build(&t, &b, xmin, ymin, xmax, ymax);
        t_build += MPI_Wtime() - tb0;

        double tf0 = MPI_Wtime();
        /* THE hybrid point: tree is read-only; ax/ay writes are disjoint
         * across i (each thread writes its own indices). Dynamic schedule
         * handles the non-uniform walk depth across clustered regions. */
        #pragma omp parallel for schedule(dynamic, 64) if(hi - lo > 128)
        for (int i = lo; i < hi; i++) {
            qtree_accel(&t, &b, i, NB_THETA, &b.ax[i], &b.ay[i]);
        }
        t_force += MPI_Wtime() - tf0;
        qtree_free(&t);

        double ti0 = MPI_Wtime();
        #pragma omp parallel for schedule(static) if(hi - lo > 1024)
        for (int i = lo; i < hi; i++) {
            b.vx[i] += b.ax[i] * dt;
            b.vy[i] += b.ay[i] * dt;
            b.px[i] += b.vx[i] * dt;
            b.py[i] += b.vy[i] * dt;
        }
        t_integ += MPI_Wtime() - ti0;

        /* Only the main thread touches MPI (THREAD_FUNNELED). */
        double tc0 = MPI_Wtime();
        MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                       b.px, counts, displs, MPI_DOUBLE, MPI_COMM_WORLD);
        MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                       b.py, counts, displs, MPI_DOUBLE, MPI_COMM_WORLD);
        t_comm += MPI_Wtime() - tc0;

        if (dump_every > 0 && rank == 0 && dump_prefix
            && ((s + 1) % dump_every == 0)) {
            char path[512];
            snprintf(path, sizeof(path), "%s_%04d.csv",
                     dump_prefix, (s + 1) / dump_every);
            bodies_write_csv(&b, path);
        }
    }

    double elapsed = MPI_Wtime() - t0;

    double mx_build, mx_force, mx_comm, mx_integ, mx_elapsed;
    MPI_Reduce(&t_build,  &mx_build,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_force,  &mx_force,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_comm,   &mx_comm,   1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&t_integ,  &mx_integ,  1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    MPI_Reduce(&elapsed,  &mx_elapsed,1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    /* Per-rank phase timings to rank 0 for load-balance analysis (TIMING_CSV). */
    double local_t[5] = { t_build, t_force, t_comm, t_integ, elapsed };
    double *all_t = NULL;
    if (rank == 0) all_t = (double*)malloc((size_t)size * 5 * sizeof(double));
    MPI_Gather(local_t, 5, MPI_DOUBLE, all_t, 5, MPI_DOUBLE, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        const char *tcsv = getenv("TIMING_CSV");
        if (tcsv) {
            FILE *tf = fopen(tcsv, "w");
            if (tf) {
                fprintf(tf, "rank,build,force,comm,integ,total\n");
                for (int r = 0; r < size; r++) {
                    double *row = &all_t[r * 5];
                    fprintf(tf, "%d,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                            r, row[0], row[1], row[2], row[3], row[4]);
                }
                fclose(tf);
            }
        }
        free(all_t);
    }

    if (rank == 0) {
        printf("hybrid  n=%d p=%d t=%d steps=%d dt=%g  "
               "time=%.4fs  build=%.4fs  force=%.4fs  comm=%.4fs  integ=%.4fs\n",
               n, size, threads, steps, dt,
               mx_elapsed, mx_build, mx_force, mx_comm, mx_integ);
    }

    if (out) {
        MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                       b.vx, counts, displs, MPI_DOUBLE, MPI_COMM_WORLD);
        MPI_Allgatherv(MPI_IN_PLACE, 0, MPI_DATATYPE_NULL,
                       b.vy, counts, displs, MPI_DOUBLE, MPI_COMM_WORLD);
        if (rank == 0) bodies_write_csv(&b, out);
    }

    free(counts); free(displs);
    bodies_free(&b);
    MPI_Finalize();
    return 0;
}
