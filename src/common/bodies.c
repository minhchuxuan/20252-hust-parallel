/* bodies.c - allocation, init, I/O, bbox, integrator half-steps, timing */
/* Request POSIX clock_gettime/CLOCK_MONOTONIC even under a strict -std=cNN. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "nbody.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>

Bodies bodies_alloc(int n) {
    Bodies b;
    b.n  = n;
    b.px = (double*)calloc(n, sizeof(double));
    b.py = (double*)calloc(n, sizeof(double));
    b.vx = (double*)calloc(n, sizeof(double));
    b.vy = (double*)calloc(n, sizeof(double));
    b.ax = (double*)calloc(n, sizeof(double));
    b.ay = (double*)calloc(n, sizeof(double));
    b.m  = (double*)calloc(n, sizeof(double));
    if (!b.px || !b.py || !b.vx || !b.vy || !b.ax || !b.ay || !b.m) {
        fprintf(stderr, "bodies_alloc: OOM n=%d\n", n);
        exit(1);
    }
    return b;
}

void bodies_free(Bodies *b) {
    if (!b) return;
    free(b->px); free(b->py);
    free(b->vx); free(b->vy);
    free(b->ax); free(b->ay);
    free(b->m);
    memset(b, 0, sizeof(*b));
}

/* Deterministic LCG-based RNG. Good enough for demo init. */
static unsigned lcg_state;
static double urand(void) {
    lcg_state = lcg_state * 1103515245u + 12345u;
    return ((lcg_state >> 16) & 0x7fff) / 32768.0;
}

/* Plummer-like distribution: sample radius from r = a / sqrt(u^{-2/3}-1),
 * angle uniform, velocity scaled to rough virial equilibrium. */
void bodies_init_plummer(Bodies *b, unsigned seed) {
    lcg_state = seed ? seed : 1u;
    const double a = 1.0;                /* Plummer scale */
    const double total_mass = 1.0;       /* standard N-body units: total mass 1 */
    const double per = total_mass / b->n;
    for (int i = 0; i < b->n; i++) {
        double u = urand(); if (u < 1e-6) u = 1e-6;
        double r = a / sqrt(pow(u, -2.0/3.0) - 1.0);
        double th = 2.0 * M_PI * urand();
        b->px[i] = r * cos(th);
        b->py[i] = r * sin(th);
        /* Tangential velocity near the circular speed sqrt(G M(<r)/r); the
         * 0.9 factor keeps the cluster close to virial equilibrium so it
         * neither collapses nor flies apart over the simulated interval. */
        double v = 0.9 * sqrt(NB_G * total_mass * (r*r) / pow(r*r + a*a, 1.5));
        double vth = th + M_PI / 2.0;
        b->vx[i] = v * cos(vth);
        b->vy[i] = v * sin(vth);
        b->m[i]  = per;
    }
}

int bodies_write_csv(const Bodies *b, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return -1; }
    fprintf(f, "id,px,py,vx,vy,m\n");
    for (int i = 0; i < b->n; i++) {
        fprintf(f, "%d,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                i, b->px[i], b->py[i], b->vx[i], b->vy[i], b->m[i]);
    }
    fclose(f);
    return 0;
}

int bodies_read_csv(Bodies *b, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; }
    int i = 0;
    while (fgets(line, sizeof(line), f) && i < b->n) {
        int id;
        if (sscanf(line, "%d,%lf,%lf,%lf,%lf,%lf",
                   &id, &b->px[i], &b->py[i], &b->vx[i], &b->vy[i], &b->m[i]) == 6) {
            i++;
        }
    }
    fclose(f);
    return i;
}

int bodies_count_csv(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    char line[512];
    if (!fgets(line, sizeof(line), f)) { fclose(f); return -1; } /* header */
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        const char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '\r' || *p == '\0') continue;   /* blank line */
        n++;
    }
    fclose(f);
    return n;
}

void bodies_bbox(const Bodies *b,
                 double *xmin, double *ymin,
                 double *xmax, double *ymax) {
    double xl = b->px[0], xh = b->px[0];
    double yl = b->py[0], yh = b->py[0];
    for (int i = 1; i < b->n; i++) {
        if (b->px[i] < xl) xl = b->px[i];
        if (b->px[i] > xh) xh = b->px[i];
        if (b->py[i] < yl) yl = b->py[i];
        if (b->py[i] > yh) yh = b->py[i];
    }
    /* pad a hair so all particles lie strictly inside */
    double pad = 1e-6 + 1e-6 * fmax(xh - xl, yh - yl);
    *xmin = xl - pad; *ymin = yl - pad;
    *xmax = xh + pad; *ymax = yh + pad;
    /* square it up: Barnes-Hut works better on square cells */
    double dx = *xmax - *xmin, dy = *ymax - *ymin;
    if (dx > dy) {
        double mid = 0.5 * (*ymin + *ymax);
        *ymin = mid - dx * 0.5; *ymax = mid + dx * 0.5;
    } else {
        double mid = 0.5 * (*xmin + *xmax);
        *xmin = mid - dy * 0.5; *xmax = mid + dy * 0.5;
    }
}

/* Semi-implicit (Euler-Cromer) kick: velocity uses the current acceleration. */
void bodies_kick(Bodies *b, int lo, int hi, double dt) {
    for (int i = lo; i < hi; i++) {
        b->vx[i] += b->ax[i] * dt;
        b->vy[i] += b->ay[i] * dt;
    }
}

void bodies_drift(Bodies *b, int lo, int hi, double dt) {
    for (int i = lo; i < hi; i++) {
        b->px[i] += b->vx[i] * dt;
        b->py[i] += b->vy[i] * dt;
    }
}

double wall_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
