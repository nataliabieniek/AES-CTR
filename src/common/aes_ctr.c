#include "aes_ctr.h"
#include <string.h>

static uint8_t sbox[256];
static int sbox_ready = 0;

static uint8_t xtime(uint8_t x) { return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b)); }

static uint8_t rotl8(uint8_t x, int s) { return (uint8_t)((x << s) | (x >> (8 - s))); }

static void sbox_init(void) {
    uint8_t p = 1, q = 1;
    do {
        p = (uint8_t)(p ^ xtime(p));          /* p *= 3 */
        q ^= (uint8_t)(q << 1);               /* q /= 3 */
        q ^= (uint8_t)(q << 2);
        q ^= (uint8_t)(q << 4);
        if (q & 0x80) q ^= 0x09;
        sbox[p] = (uint8_t)(q ^ rotl8(q, 1) ^ rotl8(q, 2) ^ rotl8(q, 3) ^ rotl8(q, 4) ^ 0x63);
    } while (p != 1);
    sbox[0] = 0x63;
    sbox_ready = 1;
}

const uint8_t *aes_sbox(void) {
    if (!sbox_ready) sbox_init();
    return sbox;
}

/* S-box jest inicjalizowany tu (przed sekcjami rownoleglymi), wiec watki tylko czytaja. */
int aes_key_init(aes_key *k, const uint8_t *key, size_t key_len) {
    if (key_len != 16 && key_len != 24 && key_len != 32) return -1;
    aes_sbox();
    int nk = (int)(key_len / 4);
    k->rounds = nk + 6;
    int total = 4 * (k->rounds + 1);
    memcpy(k->rk, key, key_len);
    uint8_t rcon = 1;
    for (int i = nk; i < total; i++) {
        uint8_t t[4];
        memcpy(t, k->rk + 4 * (i - 1), 4);
        if (i % nk == 0) {
            uint8_t a = t[0];
            t[0] = (uint8_t)(sbox[t[1]] ^ rcon);
            t[1] = sbox[t[2]];
            t[2] = sbox[t[3]];
            t[3] = sbox[a];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            for (int j = 0; j < 4; j++) t[j] = sbox[t[j]];
        }
        for (int j = 0; j < 4; j++) k->rk[4 * i + j] = (uint8_t)(k->rk[4 * (i - nk) + j] ^ t[j]);
    }
    return 0;
}

void aes_encrypt_block(const aes_key *k, const uint8_t in[16], uint8_t out[16]) {
    uint8_t s[16], t[16];
    for (int i = 0; i < 16; i++) s[i] = (uint8_t)(in[i] ^ k->rk[i]);
    for (int r = 1; r <= k->rounds; r++) {
        for (int c = 0; c < 4; c++)           /* SubBytes + ShiftRows */
            for (int row = 0; row < 4; row++)
                t[4 * c + row] = sbox[s[4 * ((c + row) % 4) + row]];
        if (r != k->rounds) {                 /* MixColumns */
            for (int c = 0; c < 4; c++) {
                uint8_t *a = t + 4 * c;
                uint8_t a0 = a[0], all = (uint8_t)(a[0] ^ a[1] ^ a[2] ^ a[3]);
                a[0] ^= (uint8_t)(all ^ xtime((uint8_t)(a[0] ^ a[1])));
                a[1] ^= (uint8_t)(all ^ xtime((uint8_t)(a[1] ^ a[2])));
                a[2] ^= (uint8_t)(all ^ xtime((uint8_t)(a[2] ^ a[3])));
                a[3] ^= (uint8_t)(all ^ xtime((uint8_t)(a[3] ^ a0)));
            }
        }
        for (int i = 0; i < 16; i++) s[i] = (uint8_t)(t[i] ^ k->rk[16 * r + i]);
    }
    memcpy(out, s, 16);
}

void ctr_add(const uint8_t base[16], uint64_t n, uint8_t out[16]) {
    unsigned carry = 0;
    for (int i = 15; i >= 0; i--) {
        unsigned sum = base[i] + (unsigned)(n & 0xff) + carry;
        out[i] = (uint8_t)sum;
        carry = sum >> 8;
        n >>= 8;
    }
}

void aes_ctr_xcrypt(const aes_key *k, const uint8_t base[16], uint64_t first_block,
                    const uint8_t *in, uint8_t *out, size_t len) {
    uint8_t ctr[16], ks[16];
    for (size_t off = 0; off < len; off += AES_BLOCK) {
        ctr_add(base, first_block + off / AES_BLOCK, ctr);
        aes_encrypt_block(k, ctr, ks);
        size_t n = len - off < AES_BLOCK ? len - off : AES_BLOCK;
        for (size_t i = 0; i < n; i++) out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
    }
}
