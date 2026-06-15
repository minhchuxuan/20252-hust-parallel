/* tests/compare_csv.c - compare two CSV files row-by-row with tolerance.
 *
 *   ./compare_csv a.csv b.csv [rtol=1e-6] [atol=1e-9]
 *
 * Exit 0 if all numeric fields agree within max(atol, rtol*max(|a|,|b|));
 * exit 1 on first significant mismatch (prints row, col, values, delta).
 *
 * Used by tests/run_tests.sh to check serial==parallel. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int split_commas(char *line, double *vals, int maxn) {
    int n = 0;
    char *p = line;
    char *tok = p;
    while (*p && n < maxn) {
        if (*p == ',' || *p == '\n' || *p == '\r') {
            char save = *p; *p = 0;
            /* First column ("id") is an int but parsing as double is fine. */
            vals[n++] = strtod(tok, NULL);
            *p = save;
            if (*p == '\n' || *p == '\r') break;
            tok = p + 1;
        }
        p++;
    }
    if (n < maxn && *tok) vals[n++] = strtod(tok, NULL);
    return n;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s a.csv b.csv [rtol=1e-6] [atol=1e-9]\n", argv[0]);
        return 2;
    }
    double rtol = (argc >= 4) ? atof(argv[3]) : 1e-6;
    double atol = (argc >= 5) ? atof(argv[4]) : 1e-9;

    FILE *fa = fopen(argv[1], "r"); if (!fa) { perror(argv[1]); return 2; }
    FILE *fb = fopen(argv[2], "r"); if (!fb) { perror(argv[2]); return 2; }

    char la[1024], lb[1024];
    /* skip headers */
    if (!fgets(la, sizeof(la), fa) || !fgets(lb, sizeof(lb), fb)) {
        fprintf(stderr, "empty input\n"); return 2;
    }

    int row = 0;
    double va[16], vb[16];
    double max_abs = 0.0, max_rel = 0.0;
    while (fgets(la, sizeof(la), fa)) {
        if (!fgets(lb, sizeof(lb), fb)) {
            fprintf(stderr, "FAIL: %s has more rows than %s\n", argv[1], argv[2]);
            return 1;
        }
        int na = split_commas(la, va, 16);
        int nb = split_commas(lb, vb, 16);
        if (na != nb) {
            fprintf(stderr, "FAIL: row %d column count %d vs %d\n", row, na, nb);
            return 1;
        }
        for (int k = 0; k < na; k++) {
            double d = fabs(va[k] - vb[k]);
            double m = fmax(fabs(va[k]), fabs(vb[k]));
            double tol = fmax(atol, rtol * m);
            if (d > max_abs) max_abs = d;
            if (m > 0.0 && d / m > max_rel) max_rel = d / m;
            if (d > tol) {
                fprintf(stderr,
                    "FAIL: row %d col %d  a=%.17g  b=%.17g  |d|=%.3g tol=%.3g\n",
                    row, k, va[k], vb[k], d, tol);
                return 1;
            }
        }
        row++;
    }
    if (fgets(lb, sizeof(lb), fb)) {
        fprintf(stderr, "FAIL: %s has more rows than %s\n", argv[2], argv[1]);
        return 1;
    }
    fclose(fa); fclose(fb);
    printf("OK: %d rows match  (max |d|=%.3g, max rel=%.3g, rtol=%g atol=%g)\n",
           row, max_abs, max_rel, rtol, atol);
    return 0;
}
