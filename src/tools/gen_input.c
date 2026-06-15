/* tools/gen_input.c - generate CSV initial conditions for Barnes-Hut.
 *
 *   ./gen_input N OUT.csv [seed] [mode]
 *
 * mode:
 *   plummer  (default) - Plummer cluster, same as the built-in init
 *   disk               - rotating disk (toy spiral)
 *   two                - two Plummer clusters on collision course
 */
#include "../common/nbody.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned lcg;
static double urand(void) {
    lcg = lcg * 1103515245u + 12345u;
    return ((lcg >> 16) & 0x7fff) / 32768.0;
}

static void gen_disk(Bodies *b) {
    const double M = 1.0;                 /* standard N-body units: total mass 1 */
    const double per = M / b->n;
    for (int i = 0; i < b->n; i++) {
        double r = 0.1 + 2.0 * sqrt(urand());
        double th = 2.0 * M_PI * urand();
        b->px[i] = r * cos(th);
        b->py[i] = r * sin(th);
        double v = sqrt(NB_G * M / (r + 0.1)) * 0.9;   /* near-circular orbit */
        b->vx[i] = -v * sin(th);
        b->vy[i] =  v * cos(th);
        b->m[i]  = per;
    }
}

static void gen_two(Bodies *b) {
    int half = b->n / 2;
    const double M = 1.0;                 /* total mass 1, split into two clumps */
    const double per = M / b->n;
    const double Mclump = 0.5 * M;        /* each clump carries half the mass */
    for (int i = 0; i < b->n; i++) {
        int g = (i < half) ? 0 : 1;
        double offx = g ? 4.0 : -4.0;
        double u = urand(); if (u < 1e-6) u = 1e-6;
        double r = 0.5 / sqrt(pow(u, -2.0/3.0) - 1.0);
        double th = 2.0 * M_PI * urand();
        b->px[i] = offx + r * cos(th);
        b->py[i] = r * sin(th);
        double vdrift = g ? -0.25 : 0.25;                 /* gentle approach */
        double v = 0.9 * sqrt(NB_G * Mclump / (r + 0.2)); /* internal orbit */
        b->vx[i] = vdrift + v * cos(th + M_PI/2);
        b->vy[i] = v * sin(th + M_PI/2);
        b->m[i]  = per;
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s N OUT.csv [seed=42] [plummer|disk|two]\n", argv[0]);
        return 1;
    }
    int n = atoi(argv[1]);
    const char *out = argv[2];
    unsigned seed = (argc >= 4) ? (unsigned)atoi(argv[3]) : 42u;
    const char *mode = (argc >= 5) ? argv[4] : "plummer";

    if (n <= 0) { fprintf(stderr, "bad N\n"); return 1; }

    Bodies b = bodies_alloc(n);
    lcg = seed ? seed : 1u;

    if      (strcmp(mode, "plummer") == 0) bodies_init_plummer(&b, seed);
    else if (strcmp(mode, "disk")    == 0) gen_disk(&b);
    else if (strcmp(mode, "two")     == 0) gen_two(&b);
    else { fprintf(stderr, "unknown mode: %s\n", mode); return 1; }

    if (bodies_write_csv(&b, out) != 0) return 1;
    printf("wrote %d bodies to %s (mode=%s seed=%u)\n", n, out, mode, seed);
    bodies_free(&b);
    return 0;
}
