/* tools/hello_mpi.c - cluster smoke test (task.md §4 verification).
 *
 *   mpicc -o hello_mpi src/tools/hello_mpi.c
 *   mpirun -np 3 -hostfile scripts/hostfile.openmpi ./hello_mpi
 *
 * Prints rank/size/hostname from each process; rank 0 additionally prints
 * the MPI version. This is the M3 tick in task.md. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L   /* gethostname() under a strict -std=cNN */
#endif
#include <mpi.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    char host[256] = {0};
    gethostname(host, sizeof(host) - 1);

    if (rank == 0) {
        char ver[MPI_MAX_LIBRARY_VERSION_STRING];
        int vlen = 0;
        MPI_Get_library_version(ver, &vlen);
        /* Keep it to one short line */
        char *nl = strchr(ver, '\n'); if (nl) *nl = 0;
        printf("[master] MPI library: %s\n", ver);
    }
    /* Print in rank order for readable output. */
    for (int r = 0; r < size; r++) {
        MPI_Barrier(MPI_COMM_WORLD);
        if (r == rank) {
            printf("rank %d/%d on %s\n", rank, size, host);
            fflush(stdout);
        }
    }
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
    return 0;
}
