/* OpenMP: watki dostaja rozlaczne zakresy blokow wraz z ich licznikami.
 * Pomysl: oddziel rownoleglosc obliczen od buforowania pliku. */
#include <omp.h>
#include <stdio.h>
#include "../common/aes_ctr.h"
#include "../common/cli.h"

/* Szyfruje bufor rownolegle; first_block = globalny numer pierwszego bloku bufora. */
static void process_buffer(const aes_key *key, const cli_args *a, uint64_t first_block,
                           const uint8_t *in, uint8_t *out, size_t n) {
    /* TODO: #pragma omp parallel for po blokach (lub zakresach blokow na watek),
     * wywolanie aes_ctr_xcrypt() z wlasciwym first_block */
    (void)key; (void)a; (void)first_block; (void)in; (void)out; (void)n;
}

int main(int argc, char **argv) {
    cli_args a;
    if (cli_parse(argc, argv, &a)) return 1;
    /* TODO: omp_set_num_threads(a.threads) gdy > 0 */
    /* TODO: aes_key_init, otwarcie plikow, rozmiar pliku, cli_check_counter_range */
    /* TODO: petla: fread -> process_buffer -> fwrite (wersja 2 osob: podwojne buforowanie) */
    /* TODO: pomiar: czas jadra vs caly potok, wypisz jedna linie z GB/s */
    (void)process_buffer;
    return 0;
}
