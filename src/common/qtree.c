/* qtree.c - Barnes-Hut quadtree (2D) with center-of-mass and force walk. */
#include "nbody.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void qtree_reserve(QTree *t, int need) {
    if (need <= t->cap) return;
    int cap = t->cap ? t->cap : 1024;
    while (cap < need) cap *= 2;
    t->nodes = (QNode*)realloc(t->nodes, cap * sizeof(QNode));
    if (!t->nodes) { fprintf(stderr, "qtree_reserve: OOM\n"); exit(1); }
    t->cap = cap;
}

static int qtree_new(QTree *t, double xmin, double ymin, double xmax, double ymax) {
    qtree_reserve(t, t->n + 1);
    int id = t->n++;
    QNode *q = &t->nodes[id];
    q->xmin = xmin; q->ymin = ymin; q->xmax = xmax; q->ymax = ymax;
    q->cx = q->cy = q->mass = 0.0;
    q->child[0] = q->child[1] = q->child[2] = q->child[3] = -1;
    q->body = -1;
    q->is_internal = 0;
    q->merged = 0;
    return id;
}

/* Which child quadrant does (x,y) fall into, relative to node? */
static int quadrant(const QNode *q, double x, double y) {
    double mx = 0.5 * (q->xmin + q->xmax);
    double my = 0.5 * (q->ymin + q->ymax);
    int east = (x >= mx);
    int north = (y >= my);
    return (north << 1) | east;  /* 0=SW,1=SE,2=NW,3=NE */
}

static void child_bbox(const QNode *q, int k,
                       double *xmin, double *ymin, double *xmax, double *ymax) {
    double mx = 0.5 * (q->xmin + q->xmax);
    double my = 0.5 * (q->ymin + q->ymax);
    int east  = k & 1;
    int north = (k >> 1) & 1;
    *xmin = east  ? mx : q->xmin;
    *xmax = east  ? q->xmax : mx;
    *ymin = north ? my : q->ymin;
    *ymax = north ? q->ymax : my;
}

/* Insert body idx into subtree rooted at `root`. Iterative with an inner
 * recursive call only for the leaf-split case (old body re-insert). */
static void qtree_insert_body(QTree *t, const Bodies *b, int root, int idx) {
    double x = b->px[idx], y = b->py[idx];
    int cur = root;
    for (;;) {
        QNode *q = &t->nodes[cur];
        if ((q->xmax - q->xmin) < 1e-12) {
            /* Cell can no longer be subdivided (bodies coincide to < 1e-12).
             * Accumulate them into one merged pseudo-particle rather than
             * overwriting, which would silently discard a body's mass. */
            if (!q->merged && q->body < 0) {
                q->body = idx;                 /* first body: plain leaf */
            } else {
                if (!q->merged) {              /* promote the existing body */
                    int old = q->body;
                    q->mass = b->m[old];
                    q->cx   = b->px[old];
                    q->cy   = b->py[old];
                    q->body = -1;
                    q->merged = 1;
                }
                double mi = b->m[idx];
                double M  = q->mass + mi;
                q->cx = (q->cx * q->mass + b->px[idx] * mi) / M;
                q->cy = (q->cy * q->mass + b->py[idx] * mi) / M;
                q->mass = M;
            }
            return;
        }
        if (!q->is_internal && q->body < 0) {
            q->body = idx;
            return;
        }
        if (!q->is_internal && q->body >= 0) {
            /* split */
            int old = q->body;
            q->is_internal = 1;
            q->body = -1;
            /* reinsert old body into subtree, then loop to insert new */
            int k_old = quadrant(q, b->px[old], b->py[old]);
            if (q->child[k_old] < 0) {
                double a,bb,c,d;
                child_bbox(q, k_old, &a,&bb,&c,&d);
                int ch = qtree_new(t, a,bb,c,d);
                q = &t->nodes[cur];
                q->child[k_old] = ch;
            }
            /* place old directly in that child (which is empty) */
            {
                int ch = t->nodes[cur].child[k_old];
                /* child might itself need subdivision if new body also lands
                 * in same quadrant; let recursive call handle it. */
                qtree_insert_body(t, b, ch, old);
            }
            /* continue descending with the new body */
            q = &t->nodes[cur];
            int k = quadrant(q, x, y);
            if (q->child[k] < 0) {
                double a,bb,c,d;
                child_bbox(q, k, &a,&bb,&c,&d);
                int ch = qtree_new(t, a,bb,c,d);
                q = &t->nodes[cur];
                q->child[k] = ch;
            }
            cur = t->nodes[cur].child[k];
            continue;
        }
        /* internal */
        int k = quadrant(q, x, y);
        if (q->child[k] < 0) {
            double a,bb,c,d;
            child_bbox(q, k, &a,&bb,&c,&d);
            int ch = qtree_new(t, a,bb,c,d);
            q = &t->nodes[cur];
            q->child[k] = ch;
        }
        cur = t->nodes[cur].child[k];
    }
}

/* Compute center-of-mass for all nodes (post-order). */
static void qtree_compute_com(QTree *t, const Bodies *b, int id) {
    QNode *q = &t->nodes[id];
    if (!q->is_internal) {
        if (q->merged) {
            /* mass/cx/cy already accumulated at insertion time */
        } else if (q->body >= 0) {
            q->mass = b->m[q->body];
            q->cx   = b->px[q->body];
            q->cy   = b->py[q->body];
        } else {
            q->mass = 0.0; q->cx = 0.0; q->cy = 0.0;
        }
        return;
    }
    double mm = 0.0, cx = 0.0, cy = 0.0;
    for (int k = 0; k < 4; k++) {
        int ch = q->child[k];
        if (ch < 0) continue;
        qtree_compute_com(t, b, ch);
        double cm = t->nodes[ch].mass;
        mm += cm;
        cx += cm * t->nodes[ch].cx;
        cy += cm * t->nodes[ch].cy;
    }
    q = &t->nodes[id];  /* in case of realloc during recursion */
    q->mass = mm;
    if (mm > 0.0) { q->cx = cx / mm; q->cy = cy / mm; }
    else { q->cx = 0.0; q->cy = 0.0; }
}

void qtree_build(QTree *t, const Bodies *b,
                 double xmin, double ymin, double xmax, double ymax) {
    t->nodes = NULL; t->n = 0; t->cap = 0;
    qtree_reserve(t, 2 * b->n + 16);
    int root = qtree_new(t, xmin, ymin, xmax, ymax);
    for (int i = 0; i < b->n; i++) {
        qtree_insert_body(t, b, root, i);
    }
    qtree_compute_com(t, b, root);
}

void qtree_free(QTree *t) {
    free(t->nodes);
    t->nodes = NULL; t->n = 0; t->cap = 0;
}

/* Barnes-Hut force on body i. Iterative with explicit stack. */
void qtree_accel(const QTree *t, const Bodies *b, int i,
                 double theta, double *axo, double *ayo) {
    double ax = 0.0, ay = 0.0;
    double xi = b->px[i], yi = b->py[i];
    /* Explicit DFS stack. Depth is bounded by 4x the tree height; the height is
     * capped by the 1e-12 subdivision floor (~50 levels for any realistic box),
     * so STACK_CAP is comfortably safe. We still abort on overflow rather than
     * silently dropping a subtree (which would corrupt the force). */
    enum { STACK_CAP = 512 };
    int stack[STACK_CAP];
    int sp = 0;
    if (t->n > 0) stack[sp++] = 0;
    const double soft2 = NB_SOFTEN * NB_SOFTEN;
    while (sp > 0) {
        int id = stack[--sp];
        const QNode *q = &t->nodes[id];
        if (q->mass <= 0.0) continue;
        double dx = q->cx - xi;
        double dy = q->cy - yi;
        double r2 = dx*dx + dy*dy + soft2;
        double s  = q->xmax - q->xmin;
        if (!q->is_internal) {
            /* leaf: skip an ordinary leaf that is empty or holds body i itself.
             * A merged leaf (coincident bodies) keeps body < 0 but has mass, so
             * it must still contribute. */
            if (!q->merged && (q->body == i || q->body < 0)) continue;
            double r = sqrt(r2);
            double inv3 = 1.0 / (r2 * r);
            ax += NB_G * q->mass * dx * inv3;
            ay += NB_G * q->mass * dy * inv3;
        } else if ((s * s) < theta * theta * r2) {
            /* far enough: treat as pseudoparticle */
            double r = sqrt(r2);
            double inv3 = 1.0 / (r2 * r);
            ax += NB_G * q->mass * dx * inv3;
            ay += NB_G * q->mass * dy * inv3;
        } else {
            /* too close: recurse into children */
            for (int k = 0; k < 4; k++) {
                int ch = q->child[k];
                if (ch < 0) continue;
                if (sp >= STACK_CAP) {
                    fprintf(stderr, "qtree_accel: walk stack overflow "
                            "(depth cap %d exceeded) for body %d\n", STACK_CAP, i);
                    exit(1);
                }
                stack[sp++] = ch;
            }
        }
    }
    *axo = ax; *ayo = ay;
}
