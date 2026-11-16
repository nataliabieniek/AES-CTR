/* AES-CTR, wersja OpenMP.
 *   --double 0  odczyt -> szyfrowanie -> zapis, bufor po buforze
 *   --double 1  dwie pary buforow: w czasie szyfrowania bufora k osobne zadanie
 *               zapisuje wynik k-1 i czyta k+1
 * Deszyfrowanie = ten sam program na szyfrogramie (CTR jest symetryczny). */
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/aes_ctr.h"
#include "../common/cli.h"
#include "../common/fileio.h"
#include "../common/timer.h"

/* Podwojne buforowanie: bufor jest ciety na najwyzej TASKS_PER_THREAD zadan na
 * watek, kazde co najmniej MIN_TASK_BLOCKS blokow (4 KiB). Gorny limit jest
 * konieczny: libgomp przy ponad 64 zadaniach w kolejce na watek przestaje je
 * odkladac i wykonuje od razu w watku tworzacym - duzy bufor liczylby sie
 * wtedy prawie sekwencyjnie. */
#define TASKS_PER_THREAD 8
#define MIN_TASK_BLOCKS 256

typedef struct {
    const aes_key *key;
    const uint8_t *base;  /* poczatkowy blok licznika */
    FILE *fi, *fo;
    uint64_t size;        /* rozmiar pliku wejsciowego */
    size_t buffer;
    run_stats *st;
} job;

/* Blok b bufora: XOR z AES(base + first_block + b), first_block = offset bufora
 * w pliku / 16. Licznik zalezy tylko od pozycji bloku w pliku, wiec przy
 * dowolnym podziale petli miedzy watki zakresy licznikow sa rozlaczne. */
static inline void xcrypt_block(const job *j, uint64_t first_block, const uint8_t *in,
                                uint8_t *out, size_t n, int64_t b) {
    size_t off = (size_t)b * AES_BLOCK;
    size_t len = n - off < AES_BLOCK ? n - off : AES_BLOCK; /* < 16 tylko na koncu pliku */
    aes_ctr_xcrypt(j->key, j->base, first_block + (uint64_t)b, in + off, out + off, len);
}

static size_t chunk_len(const job *j, uint64_t off) {
    uint64_t left = j->size - off;
    return left < j->buffer ? (size_t)left : j->buffer;
}

/* Niepelny odczyt/zapis traktujemy jako blad (dlugosc znamy z rozmiaru pliku). */
static int read_buf(const job *j, uint64_t off, uint8_t *buf, size_t n) {
    double t = now_sec();
    size_t got = file_read_at(j->fi, off, buf, n);
    j->st->read_s += now_sec() - t;
    if (got != n) fprintf(stderr, "Blad odczytu: oczekiwano %zu B, odczytano %zu B.\n", n, got);
    return got != n;
}

static int write_buf(const job *j, uint64_t off, const uint8_t *buf, size_t n) {
    double t = now_sec();
    size_t put = file_write_at(j->fo, off, buf, n);
    j->st->write_s += now_sec() - t;
    if (put != n) fprintf(stderr, "Blad zapisu: oczekiwano %zu B, zapisano %zu B.\n", n, put);
    return put != n;
}

static int run_sync(const job *j, uint8_t *in, uint8_t *out) {
    for (uint64_t off = 0; off < j->size;) {
        size_t n = chunk_len(j, off);
        if (read_buf(j, off, in, n)) return 1;

        const int64_t nblk = (int64_t)((n + AES_BLOCK - 1) / AES_BLOCK);
        double t = now_sec();
#pragma omp parallel for schedule(runtime)
        for (int64_t b = 0; b < nblk; b++) xcrypt_block(j, off / AES_BLOCK, in, out, n, b);
        j->st->kernel_s += now_sec() - t;

        if (write_buf(j, off, out, n)) return 1;
        off += n;
        j->st->bytes += n;
    }
    return 0;
}

/* Bufor k zajmuje in[k % 2] / out[k % 2]. W czasie jego szyfrowania zadanie I/O
 * uzywa drugiej pary: w out[] lezy jeszcze wynik k-1, a in[] jest wolne na k+1.
 * Obie strony nie dotykaja tej samej pamieci, wiec blokady sa zbedne; jedyna
 * synchronizacja to taskwait na koncu iteracji. */
static int run_double(const job *j, uint8_t *in[2], uint8_t *out[2]) {
    if (j->size == 0) return 0;
    int err = read_buf(j, 0, in[0], chunk_len(j, 0));

#pragma omp parallel
#pragma omp single
    {
        uint64_t off = 0, prev_off = 0;
        size_t n = chunk_len(j, 0), prev_n = 0;
        uint8_t *prev_out = NULL;
        const int64_t max_tasks = (int64_t)TASKS_PER_THREAD * omp_get_num_threads();
        for (int k = 0; !err && n > 0; k++) {
            uint8_t *cin = in[k % 2], *cout = out[k % 2];
            uint8_t *next_in = in[(k + 1) % 2];
            uint64_t next_off = off + n;
            size_t next_n = chunk_len(j, next_off);
            int io_err = 0;

            /* Zapis i odczyt w jednym zadaniu: I/O zajmuje naraz najwyzej
             * jeden watek. */
#pragma omp task shared(io_err)
            {
                if (prev_n) io_err |= write_buf(j, prev_off, prev_out, prev_n);
                if (next_n) io_err |= read_buf(j, next_off, next_in, next_n);
            }

            /* taskloop zamiast parallel for: fragmenty bierze watek, ktory jest
             * akurat wolny, wiec watek zajety I/O dolacza po jego zakonczeniu. */
            const int64_t nblk = (int64_t)((n + AES_BLOCK - 1) / AES_BLOCK);
            int64_t ntasks = nblk / MIN_TASK_BLOCKS;
            if (ntasks > max_tasks) ntasks = max_tasks;
            if (ntasks < 1) ntasks = 1;
            double t = now_sec();
#pragma omp taskloop num_tasks(ntasks)
            for (int64_t b = 0; b < nblk; b++) xcrypt_block(j, off / AES_BLOCK, cin, cout, n, b);
            j->st->kernel_s += now_sec() - t;

#pragma omp taskwait
            err |= io_err;
            j->st->bytes += n;
            prev_off = off;
            prev_n = n;
            prev_out = cout;
            off = next_off;
            n = next_n;
        }
        /* ostatniego bufora nie zapisalo zadne zadanie I/O */
        if (!err && prev_n) err |= write_buf(j, prev_off, prev_out, prev_n);
    }
    return err;
}

int main(int argc, char **argv) {
    cli_args a;
    if (cli_parse(argc, argv, &a)) return 1;
    run_stats st;
    stats_reset(&st);

    if (a.threads > 0) omp_set_num_threads(a.threads);
    /* Bez OMP_SCHEDULE schedule(runtime) bierze wartosc domyslna implementacji;
     * w libgomp to dynamic,1, czyli przydzial po jednym bloku 16 B. */
    if (!getenv("OMP_SCHEDULE")) omp_set_schedule(omp_sched_static, 0);

    aes_key key;
    if (aes_key_init(&key, a.key, a.key_len)) {
        fprintf(stderr, "Blad: niepoprawny klucz.\n");
        return 1;
    }
    int64_t size = file_size(a.in_path);
    if (size < 0) {
        perror(a.in_path);
        return 1;
    }
    if (cli_check_counter_range(&a, ((uint64_t)size + AES_BLOCK - 1) / AES_BLOCK)) return 1;

    FILE *fi = file_open(a.in_path, "rb");
    if (!fi) return 1;
    FILE *fo = file_open(a.out_path, "wb");
    if (!fo) {
        fclose(fi);
        return 1;
    }

    int nbuf = a.double_buffer ? 2 : 1;
    uint8_t *in[2] = {NULL, NULL}, *out[2] = {NULL, NULL};
    int rc = 0;
    for (int i = 0; i < nbuf; i++) {
        in[i] = malloc(a.buffer_size);
        out[i] = malloc(a.buffer_size);
        if (!in[i] || !out[i]) rc = 1;
    }
    if (rc) fprintf(stderr, "Blad: brak pamieci na bufory.\n");

    /* Pula watkow powstaje tutaj, zeby jej utworzenie nie weszlo do kernel_s. */
#pragma omp parallel
    { (void)0; }

    if (!rc) {
        job j = {&key, a.counter, fi, fo, (uint64_t)size, a.buffer_size, &st};
        double t_total = now_sec();
        rc = a.double_buffer ? run_double(&j, in, out) : run_sync(&j, in[0], out[0]);
        /* koncowka wyniku moze jeszcze lezec w buforze stdio - fflush w ramach total_s */
        if (fflush(fo) != 0) {
            perror("Blad oprozniania bufora wyjsciowego");
            rc = 1;
        }
        st.total_s = now_sec() - t_total;
    }

    if (fclose(fo) != 0) {
        perror("Blad zamykania pliku wyjsciowego");
        rc = 1;
    }
    fclose(fi);
    for (int i = 0; i < nbuf; i++) {
        free(in[i]);
        free(out[i]);
    }
    if (!rc) stats_report(&st, "openmp", omp_get_max_threads(), a.buffer_size);
    return rc;
}
