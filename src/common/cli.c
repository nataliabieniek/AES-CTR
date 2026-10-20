#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int hex_decode(const char *s, uint8_t *out, size_t max, size_t *len) {
    size_t n = strlen(s);
    if (n % 2 || n / 2 > max) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(s + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    *len = n / 2;
    return 0;
}

static int usage(const char *prog) {
    fprintf(stderr,
            "Uzycie: %s --in PLIK --out PLIK --key HEX(32|48|64) --counter HEX(32)\n"
            "          [--buffer BAJTY=1048576] [--threads N] [--double 0|1]\n",
            prog);
    return -1;
}

int cli_parse(int argc, char **argv, cli_args *a) {
    memset(a, 0, sizeof *a);
    a->buffer_size = 1 << 20;
    int have_ctr = 0;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return usage(argv[0]);
        const char *k = argv[i], *v = argv[i + 1];
        if (!strcmp(k, "--in")) a->in_path = v;
        else if (!strcmp(k, "--out")) a->out_path = v;
        else if (!strcmp(k, "--key")) {
            if (hex_decode(v, a->key, 32, &a->key_len) ||
                (a->key_len != 16 && a->key_len != 24 && a->key_len != 32))
                return usage(argv[0]);
        } else if (!strcmp(k, "--counter")) {
            size_t n;
            if (hex_decode(v, a->counter, 16, &n) || n != 16) return usage(argv[0]);
            have_ctr = 1;
        } else if (!strcmp(k, "--buffer")) a->buffer_size = (size_t)strtoull(v, NULL, 10);
        else if (!strcmp(k, "--threads")) a->threads = atoi(v);
        else if (!strcmp(k, "--double")) a->double_buffer = atoi(v) != 0;
        else return usage(argv[0]);
    }
    if (!a->in_path || !a->out_path || !a->key_len || !have_ctr ||
        a->buffer_size == 0 || a->buffer_size % 16)
        return usage(argv[0]);
    return 0;
}

int cli_check_counter_range(const cli_args *a, uint64_t nblocks) {
    /* ostatni uzyty blok = counter + nblocks - 1; przeniesienie poza 128 bitow = zawiniecie */
    unsigned carry = 0;
    uint64_t n = nblocks ? nblocks - 1 : 0;
    for (int i = 15; i >= 0; i--) {
        unsigned sum = a->counter[i] + (unsigned)(n & 0xff) + carry;
        carry = sum >> 8;
        n >>= 8;
    }
    if (carry) {
        fprintf(stderr, "Blad: licznik zawinalby sie - powtorzenie strumienia klucza.\n");
        return -1;
    }
    return 0;
}
