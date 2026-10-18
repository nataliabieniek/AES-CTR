#include "cli.h"

int cli_parse(int argc, char **argv, cli_args *a) {
    /* TODO: ustaw wartosci domyslne (buffer 1 MiB), sparsuj --in --out --key --counter
     * --buffer --threads (hex -> bajty), zwaliduj (dlugosc klucza, buffer % 16 == 0),
     * przy bledzie wypisz usage i zwroc -1 */
    (void)argc; (void)argv; (void)a;
    return -1;
}

int cli_check_counter_range(const cli_args *a, uint64_t nblocks) {
    /* TODO: zwroc -1, jesli counter + nblocks - 1 przekracza 2^128 - 1 (zawiniecie licznika) */
    (void)a; (void)nblocks;
    return 0;
}

double now_sec(void) {
    /* TODO: zegar monotoniczny w sekundach (clock_gettime / QueryPerformanceCounter) */
    return 0.0;
}
