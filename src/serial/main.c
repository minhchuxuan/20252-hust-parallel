/* serial/main.c - serial Barnes-Hut driver.
 *
 * Usage:
 *   ./nbody_serial  N STEPS DT [OUT.csv]
 *   ./nbody_serial  --from FILE STEPS DT [OUT.csv]
 *
 * Prints a one-line summary and writes final state to OUT.csv if given. */
#include "../common/nbody.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *p) {
    fprintf(stderr,
        "usage: %s N STEPS DT [OUT.csv]\n"
        "       %s --from FILE STEPS DT [OUT.csv]\n"
        "env: DUMP_EVERY=K DUMP_PREFIX=path/frame  (writes path/frame_0000.csv ...)\n",
        p, p);
}

/* Optional per-step trajectory dump, controlled by env vars. */
static int dump_every = 0;
static const char *dump_prefix = NULL;
static void maybe_dump(const Bodies *b, int step) {
    if (dump_every <= 0 || !dump_prefix) return;
    if (step % dump_every != 0) return;
    char path[512];
    snprintf(path, sizeof(path), "%s_%04d.csv", dump_prefix, step / dump_every);
    bodies_write_csv(b, path);
}

int main(int argc, char **argv) {
    if (argc < 4) { usage(argv[0]); return 1; }

    const char *de = getenv("DUMP_EVERY");
    const char *dp = getenv("DUMP_PREFIX");
    if (de && dp) { dump_every = atoi(de); dump_prefix = dp; }

    Bodies b;
    int steps;
    double dt;
    const char *out = NULL;

    if (strcmp(argv[1], "--from") == 0) {
        if (argc < 5) { usage(argv[0]); return 1; }
        const char *path = argv[2];
        int n = bodies_count_csv(path);
        if (n <= 0) { fprintf(stderr, "empty or unreadable input: %s\n", path); return 1; }
        b = bodies_alloc(n);
        int got = bodies_read_csv(&b, path);
        if (got != n) {
            fprintf(stderr, "error: parsed %d of %d rows in %s\n", got, n, path);
            return 1;
        }
        steps = atoi(argv[3]);
        dt    = atof(argv[4]);
        if (argc >= 6) out = argv[5];
    } else {
        int n = atoi(argv[1]);
        if (n <= 0) { usage(argv[0]); return 1; }
        b = bodies_alloc(n);
        bodies_init_plummer(&b, 42u);
        steps = atoi(argv[2]);
        dt    = atof(argv[3]);
        if (argc >= 5) out = argv[4];
    }

    double t0 = wall_time();
    double t_force = 0.0, t_build = 0.0;

    maybe_dump(&b, 0);
    for (int s = 0; s < steps; s++) {
        double xmin, ymin, xmax, ymax;
        bodies_bbox(&b, &xmin, &ymin, &xmax, &ymax);

        double tb0 = wall_time();
        QTree t;
        qtree_build(&t, &b, xmin, ymin, xmax, ymax);
        t_build += wall_time() - tb0;

        double tf0 = wall_time();
        for (int i = 0; i < b.n; i++) {
            qtree_accel(&t, &b, i, NB_THETA, &b.ax[i], &b.ay[i]);
        }
        t_force += wall_time() - tf0;

        /* Euler-Cromer (semi-implicit) integrator - matches the parallel
         * version, so serial/parallel outputs compare cleanly. */
        bodies_kick (&b, 0, b.n, dt);
        bodies_drift(&b, 0, b.n, dt);

        qtree_free(&t);
        maybe_dump(&b, s + 1);
    }

    double elapsed = wall_time() - t0;
    printf("serial  n=%d steps=%d dt=%g  time=%.4fs  build=%.4fs  force=%.4fs\n",
           b.n, steps, dt, elapsed, t_build, t_force);

    if (out) bodies_write_csv(&b, out);

    bodies_free(&b);
    return 0;
}
