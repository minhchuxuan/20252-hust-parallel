/* nbody.h - shared types and prototypes for 2D Barnes-Hut N-body */
#ifndef NBODY_H
#define NBODY_H

#include <stddef.h>
#include <math.h>

/* M_PI is an X/Open extension, not ISO C; define it if a strict -std=cNN hides
 * it so the code builds under any standard level. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Gravitational softening (prevents singularities). Chosen comparable to the
 * mean inter-particle spacing of the unit-mass Plummer model so that close
 * encounters stay resolvable at the demo timestep and total energy is
 * conserved (an under-softened model injects spurious energy and blows up). */
#define NB_SOFTEN      0.02
/* Newton's G (scaled units) */
#define NB_G           1.0
/* Barnes-Hut opening angle. Smaller = more accurate, slower. */
#define NB_THETA       0.5

/* Struct-of-arrays particle container. Keeps per-field arrays contiguous,
 * which makes MPI_Allgatherv on positions/masses trivial (one call per array). */
typedef struct {
    int n;          /* number of particles */
    double *px;     /* positions x */
    double *py;     /* positions y */
    double *vx;     /* velocities x */
    double *vy;     /* velocities y */
    double *ax;     /* accelerations x (scratch) */
    double *ay;     /* accelerations y (scratch) */
    double *m;      /* masses */
} Bodies;

Bodies bodies_alloc(int n);
void   bodies_free(Bodies *b);

/* Generate a pseudo-Plummer cluster (deterministic: fixed seed). */
void bodies_init_plummer(Bodies *b, unsigned seed);

/* CSV I/O: "id,px,py,vx,vy,m" header + rows. */
int  bodies_write_csv(const Bodies *b, const char *path);
int  bodies_read_csv (Bodies *b, const char *path);
/* Count data rows in a Bodies CSV (excludes header and blank lines); <0 on error. */
int  bodies_count_csv(const char *path);

/* ---- Quadtree ---- */

typedef struct QNode {
    double xmin, ymin, xmax, ymax; /* cell bounds */
    double cx, cy;                 /* center of mass */
    double mass;                   /* total mass inside */
    int    child[4];               /* node indices or -1 (quadrants: 0=SW,1=SE,2=NW,3=NE) */
    int    body;                   /* particle index if leaf holding one body, else -1 */
    int    is_internal;            /* 1 if has children */
    int    merged;                 /* 1 if leaf holds >1 coincident bodies (mass/cx/cy pre-summed) */
} QNode;

typedef struct {
    QNode *nodes;
    int    n;       /* number used */
    int    cap;     /* capacity */
} QTree;

/* Build a quadtree over bodies using bounding box (x/ymin..x/ymax). */
void qtree_build(QTree *t, const Bodies *b,
                 double xmin, double ymin, double xmax, double ymax);
void qtree_free(QTree *t);

/* Accumulate acceleration on body i (using position from b->px/py), using
 * Barnes-Hut walk with opening angle theta. Writes ax/ay (does NOT add). */
void qtree_accel(const QTree *t, const Bodies *b, int i,
                 double theta, double *ax, double *ay);

/* Find global bounding box from positions. */
void bodies_bbox(const Bodies *b,
                 double *xmin, double *ymin,
                 double *xmax, double *ymax);

/* Semi-implicit (Euler-Cromer) half-steps for a slice [lo,hi):
 * kick updates velocity from the current acceleration, drift updates
 * position from the just-updated velocity. */
void bodies_kick(Bodies *b, int lo, int hi, double dt);
void bodies_drift(Bodies *b, int lo, int hi, double dt);

double wall_time(void); /* seconds */

#endif
