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
} cli_args;

/* --in F --out F --key HEX --counter HEX32 [--buffer BYTES] [--threads N]
 * Zwraca 0 gdy OK, inaczej wypisuje blad/usage. */
int cli_parse(int argc, char **argv, cli_args *a);

/* Sprawdza brak powtorzen licznika: base + liczba_blokow nie moze przekroczyc 2^128. */
int cli_check_counter_range(const cli_args *a, uint64_t nblocks);

double now_sec(void);

#endif
