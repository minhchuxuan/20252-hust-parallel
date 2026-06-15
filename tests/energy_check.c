/* tests/energy_check.c - compute total kinetic + potential energy from a
 * state CSV. Used to verify energy conservation over a run.
 *
 *   ./energy_check state.csv
 * prints: "KE=<..>  PE=<..>  E=<..>"
 *
 * PE is O(N^2) direct summation — fine for the small test sizes.
 * Matches Barnes-Hut softening ε=1e-3 from nbody.h. */
#include "../src/common/nbody.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s state.csv\n", argv[0]); return 1; }
    int n = bodies_count_csv(argv[1]);
    if (n <= 0) { fprintf(stderr, "empty or unreadable: %s\n", argv[1]); return 1; }

    Bodies b = bodies_alloc(n);
    bodies_read_csv(&b, argv[1]);

    double KE = 0.0;
    for (int i = 0; i < n; i++) {
        double v2 = b.vx[i]*b.vx[i] + b.vy[i]*b.vy[i];
        KE += 0.5 * b.m[i] * v2;
    }
    double PE = 0.0;
    const double s2 = NB_SOFTEN * NB_SOFTEN;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double dx = b.px[i] - b.px[j];
            double dy = b.py[i] - b.py[j];
            double r = sqrt(dx*dx + dy*dy + s2);
            PE -= NB_G * b.m[i] * b.m[j] / r;
        }
    }
    printf("n=%d KE=%.10g PE=%.10g E=%.10g\n", n, KE, PE, KE + PE);
    bodies_free(&b);
    return 0;
}
