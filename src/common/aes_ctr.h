#ifndef AES_CTR_H
#define AES_CTR_H

#include <stddef.h>
#include <stdint.h>

#define AES_BLOCK 16

typedef struct {
    uint8_t rk[240];
    int rounds; /* 10, 12, 14 */
} aes_key;

/* Tablica S-box (inicjalizowana przy pierwszym wywolaniu; do skopiowania na GPU). */
const uint8_t *aes_sbox(void);

/* Rozwija klucz (16/24/32 B). Zwraca 0 gdy OK. */
int aes_key_init(aes_key *k, const uint8_t *key, size_t key_len);

/* Szyfruje jeden blok. */
void aes_encrypt_block(const aes_key *k, const uint8_t in[16], uint8_t out[16]);

/* counter = base + n (128-bit, big-endian). */
void ctr_add(const uint8_t base[16], uint64_t n, uint8_t out[16]);

/* Szyfruje/deszyfruje len bajtow; first_block = globalny numer pierwszego
 * bloku (licznik = base + first_block). Wynik: out = in XOR keystream.
 * Jedyna funkcja rdzenia - kazda wersja rownolegla wola ja na swoim zakresie. */
void aes_ctr_xcrypt(const aes_key *k, const uint8_t base[16], uint64_t first_block,
                    const uint8_t *in, uint8_t *out, size_t len);

#endif
