#include "aes_ctr.h"

/* TODO: S-box (tablica lub liczona) - wspolna dla wszystkich wersji */

int aes_key_init(aes_key *k, const uint8_t *key, size_t key_len) {
    /* TODO: walidacja key_len (16/24/32), k->rounds = 10/12/14, rozwiniecie klucza do k->rk */
    (void)k; (void)key; (void)key_len;
    return -1;
}

void aes_encrypt_block(const aes_key *k, const uint8_t in[16], uint8_t out[16]) {
    /* TODO: AddRoundKey, rundy (SubBytes, ShiftRows, MixColumns), ostatnia runda bez MixColumns */
    (void)k; (void)in; (void)out;
}

void ctr_add(const uint8_t base[16], uint64_t n, uint8_t out[16]) {
    /* TODO: out = base + n, 128-bit big-endian z przenoszeniem */
    (void)base; (void)n; (void)out;
}

void aes_ctr_xcrypt(const aes_key *k, const uint8_t base[16], uint64_t first_block,
                    const uint8_t *in, uint8_t *out, size_t len) {
    /* TODO: dla kazdego bloku: licznik = ctr_add(base, first_block + i),
     * keystream = aes_encrypt_block(licznik), out = in XOR keystream;
     * obsluz ostatni niepelny blok (len % 16 != 0) */
    (void)k; (void)base; (void)first_block; (void)in; (void)out; (void)len;
}
