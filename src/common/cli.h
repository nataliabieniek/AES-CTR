#ifndef CLI_H
#define CLI_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *in_path;
    const char *out_path;
    uint8_t key[32];
    size_t key_len;       /* 16/24/32 */
    uint8_t counter[16];  /* poczatkowy blok licznika */
    size_t buffer_size;   /* bajty, wielokrotnosc 16 */
    int threads;          /* OpenMP; 0 = domyslnie */
    int double_buffer;    /* 0 = synchroniczne bufory (1 osoba), 1 = podwojne buforowanie (2 osoby) */
} cli_args;

/* --in F --out F --key HEX --counter HEX32 [--buffer BYTES] [--threads N]
 * Zwraca 0 gdy OK, inaczej wypisuje blad/usage. */
/* Wywolanie: cli_args a; if (cli_parse(argc, argv, &a)) return 1;
 * Wszystkie argumenty --in --out --key --counter sa wymagane. */
int cli_parse(int argc, char **argv, cli_args *a);

/* Wywolaj raz po cli_parse z nblocks = (rozmiar_pliku + 15) / 16.
 * Zwraca 0 gdy zakres licznikow jest bezpieczny, -1 gdy by sie zawinal. */
int cli_check_counter_range(const cli_args *a, uint64_t nblocks);

#endif
