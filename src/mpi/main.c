/* MPI: SZABLON. Rank 0 czyta plik i rozsyla fragmenty (MPI_Scatterv / MPI_File_read_at),
 * kazdy rank liczy licznik z globalnego numeru bloku: aes_ctr_xcrypt(..., first_block, ...),
 * wynik zbierany w oryginalnej kolejnosci (MPI_Gatherv / MPI_File_write_at).
 * Raportowac osobno czas dystrybucji i obliczen. */
#include <mpi.h>
#include <stdio.h>
#include "../common/aes_ctr.h"
#include "../common/cli.h"

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    cli_args a;
    if (cli_parse(argc, argv, &a)) { MPI_Finalize(); return 1; }
    /* TODO: podzial pliku, dystrybucja, obliczenia, zbieranie, pomiary */
    if (rank == 0) printf("MPI szablon: %d procesow\n", size);
    MPI_Finalize();
    return 0;
}
