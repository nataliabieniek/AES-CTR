// CUDA: watek = jeden blok AES (16 B); licznik = base + globalny numer bloku.
// Plik jest przetwarzany w PARTIACH (--buffer). Wersja jest jedna, parametryzowana liczba
// "slotow" (bufor hosta pinned + bufor GPU + strumien + zdarzenia):
//   --double 0 : 1 slot  -> synchronicznie  read -> H2D -> kernel -> D2H -> write
//   --double 1 : 2 sloty -> podwojne buforowanie; gdy GPU liczy partie k, host czyta k+1
//                i zapisuje wynik k-1, a transfery jednego slotu naklada sie na kernel drugiego.
// Czasy H2D / kernel / D2H pochodza ze zdarzen CUDA (nie wymuszaja dodatkowych synchronizacji).
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
extern "C" {
#include "../common/aes_ctr.h"
#include "../common/cli.h"
#include "../common/fileio.h"
#include "../common/timer.h"
}

#define CHECK(x)                                                                          \
    do {                                                                                  \
        cudaError_t e_ = (x);                                                             \
        if (e_ != cudaSuccess) {                                                          \
            fprintf(stderr, "CUDA blad %s:%d: %s\n", __FILE__, __LINE__,                  \
                    cudaGetErrorString(e_));                                              \
            exit(1);                                                                      \
        }                                                                                 \
    } while (0)

#define THREADS 256

__constant__ uint8_t c_rk[240];   // klucze rund (wszystkie watki czytaja te same -> broadcast)
__constant__ uint8_t c_sbox[256]; // kopiowany do pamieci wspolnej na starcie bloku
__constant__ uint8_t c_base[16];  // poczatkowy blok licznika
__constant__ int c_rounds;

__device__ __forceinline__ uint8_t xtime_d(uint8_t x) {
    return (uint8_t)((x << 1) ^ ((x >> 7) * 0x1b));
}

__global__ void aes_ctr_kernel(uint64_t first_block, const uint8_t *in, uint8_t *out,
                               size_t len) {
    __shared__ uint8_t sbox[256];
    for (int i = threadIdx.x; i < 256; i += blockDim.x) sbox[i] = c_sbox[i];
    __syncthreads();

    uint64_t idx = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    uint64_t off = idx * 16;
    if (off >= len) return;

    // licznik = base + (first_block + idx), 128-bit big-endian z przenoszeniem
    uint8_t s[16], t[16];
    uint64_t n = first_block + idx;
    unsigned carry = 0;
    for (int i = 15; i >= 0; i--) {
        unsigned sum = c_base[i] + (unsigned)(n & 0xff) + carry;
        s[i] = (uint8_t)sum;
        carry = sum >> 8;
        n >>= 8;
    }

    // AES-encrypt bloku licznika (jak aes_encrypt_block na hoscie)
    for (int i = 0; i < 16; i++) s[i] ^= c_rk[i];
    int rounds = c_rounds;
    for (int r = 1; r <= rounds; r++) {
#pragma unroll
        for (int c = 0; c < 4; c++)
#pragma unroll
            for (int row = 0; row < 4; row++)
                t[4 * c + row] = sbox[s[4 * ((c + row) & 3) + row]]; // SubBytes + ShiftRows
        if (r != rounds) {                                           // MixColumns
#pragma unroll
            for (int c = 0; c < 4; c++) {
                uint8_t *p = t + 4 * c;
                uint8_t p0 = p[0], all = p[0] ^ p[1] ^ p[2] ^ p[3];
                p[0] ^= all ^ xtime_d(p[0] ^ p[1]);
                p[1] ^= all ^ xtime_d(p[1] ^ p[2]);
                p[2] ^= all ^ xtime_d(p[2] ^ p[3]);
                p[3] ^= all ^ xtime_d(p[3] ^ p0);
            }
        }
#pragma unroll
        for (int i = 0; i < 16; i++) s[i] = t[i] ^ c_rk[16 * r + i];
    }

    // XOR ze strumieniem klucza; ostatni niepelny blok: tylko len - off bajtow
    size_t m = len - off < 16 ? (size_t)(len - off) : 16;
    for (size_t i = 0; i < m; i++) out[off + i] = in[off + i] ^ s[i];
}

typedef struct {
    uint8_t *h_in, *h_out;  // pinned (cudaMallocHost) -> prawdziwe async
    uint8_t *d_in, *d_out;
    cudaStream_t stream;
    cudaEvent_t e[4];       // e0 start H2D, e1 koniec H2D, e2 koniec kernela, e3 koniec D2H
    int busy;
    uint64_t off;
    size_t n;
} slot_t;

// Czeka na koniec pracy slotu, dodaje czasy z zdarzen i zapisuje wynik do pliku.
static void finish_slot(slot_t *s, FILE *fo, run_stats *st) {
    if (!s->busy) return;
    CHECK(cudaEventSynchronize(s->e[3]));
    float ms;
    CHECK(cudaEventElapsedTime(&ms, s->e[0], s->e[1])); st->h2d_s += ms / 1000.0;
    CHECK(cudaEventElapsedTime(&ms, s->e[1], s->e[2])); st->kernel_s += ms / 1000.0;
    CHECK(cudaEventElapsedTime(&ms, s->e[2], s->e[3])); st->d2h_s += ms / 1000.0;
    double t = now_sec();
    if (file_write_at(fo, s->off, s->h_out, s->n) != s->n) {
        fprintf(stderr, "Blad zapisu pliku wyjsciowego\n");
        exit(1);
    }
    st->write_s += now_sec() - t;
    st->bytes += s->n;
    s->busy = 0;
}

int main(int argc, char **argv) {
    cli_args a;
    if (cli_parse(argc, argv, &a)) return 1;
    run_stats st;
    stats_reset(&st);

    // KROK 1: konfiguracja (poza pomiarem)
    aes_key key;
    if (aes_key_init(&key, a.key, a.key_len)) { fprintf(stderr, "Zly klucz\n"); return 1; }
    int64_t size = file_size(a.in_path);
    if (size < 0) { fprintf(stderr, "Nie mozna odczytac %s\n", a.in_path); return 1; }
    if (cli_check_counter_range(&a, ((uint64_t)size + 15) / 16)) return 1;
    FILE *fi = file_open(a.in_path, "rb");
    FILE *fo = file_open(a.out_path, "wb");
    if (!fi || !fo) return 1;

    CHECK(cudaMemcpyToSymbol(c_rk, key.rk, sizeof key.rk));
    CHECK(cudaMemcpyToSymbol(c_rounds, &key.rounds, sizeof key.rounds));
    CHECK(cudaMemcpyToSymbol(c_sbox, aes_sbox(), 256));
    CHECK(cudaMemcpyToSymbol(c_base, a.counter, 16));

    int nslots = a.double_buffer ? 2 : 1;
    slot_t slots[2];
    for (int i = 0; i < nslots; i++) {
        slot_t *s = &slots[i];
        s->busy = 0;
        CHECK(cudaMallocHost((void **)&s->h_in, a.buffer_size));
        CHECK(cudaMallocHost((void **)&s->h_out, a.buffer_size));
        CHECK(cudaMalloc((void **)&s->d_in, a.buffer_size));
        CHECK(cudaMalloc((void **)&s->d_out, a.buffer_size));
        CHECK(cudaStreamCreate(&s->stream));
        for (int j = 0; j < 4; j++) CHECK(cudaEventCreate(&s->e[j]));
    }
    // rozgrzewka: inicjalizacja kontekstu i pierwsze uruchomienie kernela (poza pomiarem)
    aes_ctr_kernel<<<1, THREADS>>>(0, slots[0].d_in, slots[0].d_out, 16);
    CHECK(cudaGetLastError());
    CHECK(cudaDeviceSynchronize());

    // KROK 2: start pomiaru calego potoku
    double t_total = now_sec();

    // KROK 3: petla po partiach. Partia k uzywa slotu k % nslots. Przed ponownym uzyciem slotu
    // konczymy jego poprzednia partie (zapis wyniku). Przy 2 slotach odczyt partii k+1
    // (host) pokrywa sie z praca GPU nad partia k, a zapis k-1 z praca nad k.
    uint64_t off = 0;
    for (uint64_t k = 0; off < (uint64_t)size; k++) {
        slot_t *s = &slots[k % nslots];
        finish_slot(s, fo, &st);

        double t = now_sec();
        size_t want = a.buffer_size;
        if ((uint64_t)size - off < want) want = (size_t)((uint64_t)size - off);
        size_t n = file_read_at(fi, off, s->h_in, want);
        st.read_s += now_sec() - t;
        if (n != want) { fprintf(stderr, "Blad odczytu pliku wejsciowego\n"); return 1; }

        s->off = off;
        s->n = n;
        s->busy = 1;
        size_t blocks = (n + 15) / 16;
        unsigned grid = (unsigned)((blocks + THREADS - 1) / THREADS);

        CHECK(cudaEventRecord(s->e[0], s->stream));
        CHECK(cudaMemcpyAsync(s->d_in, s->h_in, n, cudaMemcpyHostToDevice, s->stream));
        CHECK(cudaEventRecord(s->e[1], s->stream));
        aes_ctr_kernel<<<grid, THREADS, 0, s->stream>>>(off / 16, s->d_in, s->d_out, n);
        CHECK(cudaGetLastError());
        CHECK(cudaEventRecord(s->e[2], s->stream));
        CHECK(cudaMemcpyAsync(s->h_out, s->d_out, n, cudaMemcpyDeviceToHost, s->stream));
        CHECK(cudaEventRecord(s->e[3], s->stream));

        off += n;
    }
    // doprowadz do konca partie, ktore zostaly w slotach (po kolei: starsza pierwsza)
    uint64_t kend = 0;
    if (size > 0) kend = ((uint64_t)size + a.buffer_size - 1) / a.buffer_size;
    for (uint64_t k = kend > (uint64_t)nslots ? kend - nslots : 0; k < kend; k++)
        finish_slot(&slots[k % nslots], fo, &st);

    // KROK 4: koniec potoku; dopiero potem sprzatanie i raport
    CHECK(cudaDeviceSynchronize());
    st.total_s = now_sec() - t_total;

    fclose(fi);
    if (fclose(fo) != 0) { fprintf(stderr, "Blad zamkniecia pliku wyjsciowego\n"); return 1; }
    for (int i = 0; i < nslots; i++) {
        for (int j = 0; j < 4; j++) cudaEventDestroy(slots[i].e[j]);
        cudaStreamDestroy(slots[i].stream);
        cudaFreeHost(slots[i].h_in);
        cudaFreeHost(slots[i].h_out);
        cudaFree(slots[i].d_in);
        cudaFree(slots[i].d_out);
    }
    stats_report(&st, "cuda", nslots, a.buffer_size);
    // KROK 5: deszyfrowanie = ten sam program (--in zaszyfrowany, ten sam klucz i licznik).
    return 0;
}
