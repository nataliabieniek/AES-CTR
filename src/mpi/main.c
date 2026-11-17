/* AES-CTR, wersja MPI.
 * Plik czyta i zapisuje tylko rank 0 (pozostale wezly nie potrzebuja do niego dostepu).
 * Kazda partia (--buffer) jest dzielona na zakresy blokow 16 B miedzy rangi; ranga liczy licznik
 * z GLOBALNEGO numeru bloku: first_block = (offset_partii + przesuniecie_zakresu) / 16.
 * Wynik wraca na rank 0 i jest zapisywany pod swoim offsetem, wiec kolejnosc pliku sie zgadza.
 *   --double 0  read -> Scatterv -> szyfrowanie -> Gatherv -> write, partia po partii
 *   --double 1  nieblokujace Iscatterv/Igatherv: dystrybucja partii k+1 i zbieranie k-1
 *               nakladaja sie na szyfrowanie partii k
 * Procesy na jednej maszynie:  mpirun -n 4 ./aes_ctr_mpi ...
 * Kilka maszyn (np. BeagleBone): mpirun --hostfile hosts ... ; wolniejszy wezel dostaje mniejszy
 * zakres dzieki AES_MPI_WEIGHTS="w0,w1,..." (wagi rang; domyslnie rowne), np. "6,1".
 * Czasy: read_s/write_s - rank 0; h2d_s - dystrybucja (Scatterv), d2h_s - zbieranie (Gatherv),
 * kernel_s - szyfrowanie. Raportowane jest maksimum po rangach (najwolniejsza ranga). */
#include <limits.h>
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../common/aes_ctr.h"
#include "../common/cli.h"
#include "../common/fileio.h"
#include "../common/timer.h"

#define NCSLOT 3 /* tablice podzialu: partie k-1, k i k+1 moga byc w locie jednoczesnie */

typedef struct {
    int rank, nranks;
    double *weights;
    uint64_t size;
    size_t buffer;
} ctx;

static void die(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    MPI_Abort(MPI_COMM_WORLD, 1);
}

static size_t chunk_len(const ctx *c, uint64_t off) {
    uint64_t left = c->size - off;
    return left < c->buffer ? (size_t)left : c->buffer;
}

/* Dzieli n bajtow partii na zakresy blokow proporcjonalne do wag. Poczatki sa wielokrotnosciami
 * 16 B; niepelny blok z konca pliku trafia do ostatniej niepustej rangi. */
static void split(const ctx *c, size_t n, int *cnt, int *dsp) {
    uint64_t nblk = ((uint64_t)n + AES_BLOCK - 1) / AES_BLOCK, start = 0;
    double wsum = 0, acc = 0;
    for (int r = 0; r < c->nranks; r++) wsum += c->weights[r];
    for (int r = 0; r < c->nranks; r++) {
        acc += c->weights[r];
        uint64_t end = r == c->nranks - 1 ? nblk : (uint64_t)((double)nblk * acc / wsum);
        if (end < start) end = start;
        uint64_t b0 = start * AES_BLOCK, b1 = end * AES_BLOCK;
        if (b0 > n) b0 = n;
        if (b1 > n) b1 = n;
        dsp[r] = (int)b0;
        cnt[r] = (int)(b1 - b0);
        start = end;
    }
}

static void load_weights(ctx *c) {
    c->weights = malloc((size_t)c->nranks * sizeof *c->weights);
    if (!c->weights) die("Blad: brak pamieci.");
    for (int r = 0; r < c->nranks; r++) c->weights[r] = 1.0;
    const char *env = getenv("AES_MPI_WEIGHTS");
    if (!env || !*env) return;
    char *copy = strdup(env), *save = NULL;
    int n = 0;
    for (char *tok = strtok_r(copy, ",", &save); tok; tok = strtok_r(NULL, ",", &save), n++) {
        double w = atof(tok);
        if (n >= c->nranks || w <= 0) die("Blad: niepoprawne AES_MPI_WEIGHTS.");
        c->weights[n] = w;
    }
    if (n != c->nranks) die("Blad: AES_MPI_WEIGHTS musi miec tyle wag, ile jest rang.");
    free(copy);
}

static void encrypt_part(const aes_key *key, const uint8_t *base, uint64_t off, int displ,
                         const uint8_t *in, uint8_t *out, int len, run_stats *st) {
    if (len <= 0) return;
    double t = MPI_Wtime();
    aes_ctr_xcrypt(key, base, (off + (uint64_t)displ) / AES_BLOCK, in, out, (size_t)len);
    st->kernel_s += MPI_Wtime() - t;
}

static void read_batch(FILE *fi, uint64_t off, uint8_t *buf, size_t n, run_stats *st) {
    double t = MPI_Wtime();
    size_t got = file_read_at(fi, off, buf, n);
    st->read_s += MPI_Wtime() - t;
    if (got != n) die("Blad odczytu pliku wejsciowego.");
}

static void write_batch(FILE *fo, uint64_t off, const uint8_t *buf, size_t n, run_stats *st) {
    double t = MPI_Wtime();
    size_t put = file_write_at(fo, off, buf, n);
    st->write_s += MPI_Wtime() - t;
    if (put != n) die("Blad zapisu pliku wyjsciowego.");
}

static void run_sync(const ctx *c, const aes_key *key, const uint8_t *base, FILE *fi, FILE *fo,
                     uint8_t *full_in, uint8_t *full_out, uint8_t *loc_in, uint8_t *loc_out,
                     int *cnt, int *dsp, run_stats *st) {
    for (uint64_t off = 0; off < c->size;) {
        size_t n = chunk_len(c, off);
        split(c, n, cnt, dsp);
        if (c->rank == 0) read_batch(fi, off, full_in, n, st);

        double t = MPI_Wtime();
        MPI_Scatterv(full_in, cnt, dsp, MPI_BYTE, loc_in, cnt[c->rank], MPI_BYTE, 0, MPI_COMM_WORLD);
        st->h2d_s += MPI_Wtime() - t;

        encrypt_part(key, base, off, dsp[c->rank], loc_in, loc_out, cnt[c->rank], st);

        t = MPI_Wtime();
        MPI_Gatherv(loc_out, cnt[c->rank], MPI_BYTE, full_out, cnt, dsp, MPI_BYTE, 0, MPI_COMM_WORLD);
        st->d2h_s += MPI_Wtime() - t;

        if (c->rank == 0) write_batch(fo, off, full_out, n, st);
        off += n;
    }
}

/* Partia k uzywa pary buforow k % 2. Iscatterv k+1 jest wystawiany przed szyfrowaniem k,
 * Igatherv k po nim, a zapis k-1 odbywa sie, gdy zbieranie k jest juz w toku. */
static void run_double(const ctx *c, const aes_key *key, const uint8_t *base, FILE *fi, FILE *fo,
                       uint8_t *full_in[2], uint8_t *full_out[2], uint8_t *loc_in[2],
                       uint8_t *loc_out[2], int (*cnt)[c->nranks], int (*dsp)[c->nranks],
                       run_stats *st) {
    if (c->size == 0) return;
    const uint64_t nb = (c->size + c->buffer - 1) / c->buffer;
    MPI_Request rs[2] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL};
    MPI_Request rg[2] = {MPI_REQUEST_NULL, MPI_REQUEST_NULL};

    split(c, chunk_len(c, 0), cnt[0], dsp[0]);
    if (c->rank == 0) read_batch(fi, 0, full_in[0], chunk_len(c, 0), st);
    double t = MPI_Wtime();
    MPI_Iscatterv(full_in[0], cnt[0], dsp[0], MPI_BYTE, loc_in[0], cnt[0][c->rank], MPI_BYTE, 0,
                  MPI_COMM_WORLD, &rs[0]);
    st->h2d_s += MPI_Wtime() - t;

    for (uint64_t k = 0; k < nb; k++) {
        const int s = (int)(k % 2), cs = (int)(k % NCSLOT);
        const uint64_t off = k * c->buffer;
        const size_t n = chunk_len(c, off);

        t = MPI_Wtime();
        MPI_Wait(&rs[s], MPI_STATUS_IGNORE);
        st->h2d_s += MPI_Wtime() - t;

        if (k + 1 < nb) {
            const int s1 = (int)((k + 1) % 2), cs1 = (int)((k + 1) % NCSLOT);
            const uint64_t off1 = off + n;
            const size_t n1 = chunk_len(c, off1);
            split(c, n1, cnt[cs1], dsp[cs1]);
            if (c->rank == 0) read_batch(fi, off1, full_in[s1], n1, st);
            t = MPI_Wtime();
            MPI_Iscatterv(full_in[s1], cnt[cs1], dsp[cs1], MPI_BYTE, loc_in[s1], cnt[cs1][c->rank],
                          MPI_BYTE, 0, MPI_COMM_WORLD, &rs[s1]);
            st->h2d_s += MPI_Wtime() - t;
        }

        encrypt_part(key, base, off, dsp[cs][c->rank], loc_in[s], loc_out[s], cnt[cs][c->rank], st);

        t = MPI_Wtime();
        MPI_Igatherv(loc_out[s], cnt[cs][c->rank], MPI_BYTE, full_out[s], cnt[cs], dsp[cs], MPI_BYTE,
                     0, MPI_COMM_WORLD, &rg[s]);
        if (k > 0) MPI_Wait(&rg[1 - s], MPI_STATUS_IGNORE);
        st->d2h_s += MPI_Wtime() - t;
        if (k > 0 && c->rank == 0)
            write_batch(fo, off - c->buffer, full_out[1 - s], c->buffer, st);
    }
    const int last = (int)((nb - 1) % 2);
    double t2 = MPI_Wtime();
    MPI_Wait(&rg[last], MPI_STATUS_IGNORE);
    st->d2h_s += MPI_Wtime() - t2;
    if (c->rank == 0) {
        uint64_t off = (nb - 1) * c->buffer;
        write_batch(fo, off, full_out[last], chunk_len(c, off), st);
    }
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    ctx c = {0};
    MPI_Comm_rank(MPI_COMM_WORLD, &c.rank);
    MPI_Comm_size(MPI_COMM_WORLD, &c.nranks);
    cli_args a;
    if (cli_parse(argc, argv, &a)) {
        MPI_Finalize();
        return 1;
    }
    run_stats st;
    stats_reset(&st);

    aes_key key;
    if (aes_key_init(&key, a.key, a.key_len)) die("Blad: niepoprawny klucz.");
    if (a.buffer_size == 0 || a.buffer_size > (size_t)INT_MAX)
        die("Blad: --buffer musi byc w zakresie 16..2147483647 B (liczniki MPI sa typu int).");
    load_weights(&c);

    /* Rank 0 zna plik; reszta dostaje rozmiar (-1 = blad). */
    FILE *fi = NULL, *fo = NULL;
    int64_t size = 0;
    if (c.rank == 0) {
        size = file_size(a.in_path);
        if (size < 0) {
            perror(a.in_path);
        } else if (cli_check_counter_range(&a, ((uint64_t)size + AES_BLOCK - 1) / AES_BLOCK)) {
            size = -1;
        } else {
            fi = file_open(a.in_path, "rb");
            fo = file_open(a.out_path, "wb");
            if (!fi || !fo) size = -1;
        }
    }
    MPI_Bcast(&size, 1, MPI_INT64_T, 0, MPI_COMM_WORLD);
    if (size < 0) {
        MPI_Finalize();
        return 1;
    }
    c.size = (uint64_t)size;
    c.buffer = a.buffer_size;

    const int nbuf = a.double_buffer ? 2 : 1;
    uint8_t *full_in[2] = {NULL, NULL}, *full_out[2] = {NULL, NULL};
    uint8_t *loc_in[2] = {NULL, NULL}, *loc_out[2] = {NULL, NULL};
    int (*cnt)[c.nranks] = malloc(sizeof(int[NCSLOT][c.nranks]));
    int (*dsp)[c.nranks] = malloc(sizeof(int[NCSLOT][c.nranks]));
    if (!cnt || !dsp) die("Blad: brak pamieci.");
    /* Bufor lokalny: zakres tej rangi w pelnej partii; w mniejszej partii moze byc o blok wiekszy. */
    split(&c, c.buffer, cnt[0], dsp[0]);
    const size_t loc_cap = (size_t)cnt[0][c.rank] + 2 * AES_BLOCK;
    for (int i = 0; i < nbuf; i++) {
        if (c.rank == 0) {
            full_in[i] = malloc(c.buffer);
            full_out[i] = malloc(c.buffer);
            if (!full_in[i] || !full_out[i]) die("Blad: brak pamieci na bufory.");
        }
        loc_in[i] = malloc(loc_cap);
        loc_out[i] = malloc(loc_cap);
        if (!loc_in[i] || !loc_out[i]) die("Blad: brak pamieci na bufory.");
    }

    MPI_Barrier(MPI_COMM_WORLD);
    const double t_total = MPI_Wtime();
    if (a.double_buffer)
        run_double(&c, &key, a.counter, fi, fo, full_in, full_out, loc_in, loc_out, cnt, dsp, &st);
    else
        run_sync(&c, &key, a.counter, fi, fo, full_in[0], full_out[0], loc_in[0], loc_out[0], cnt[0],
                 dsp[0], &st);
    if (c.rank == 0 && fflush(fo) != 0) die("Blad oprozniania pliku wyjsciowego.");
    MPI_Barrier(MPI_COMM_WORLD);
    st.total_s = MPI_Wtime() - t_total;
    st.bytes = c.size;

    int rc = 0;
    if (c.rank == 0) {
        if (fclose(fo) != 0) {
            perror("Blad zamykania pliku wyjsciowego");
            rc = 1;
        }
        fclose(fi);
    }

    /* Najwolniejsza ranga wyznacza czas kazdej fazy. */
    double mine[5] = {st.read_s, st.h2d_s, st.kernel_s, st.d2h_s, st.write_s}, mx[5];
    MPI_Reduce(mine, mx, 5, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    if (c.rank == 0) {
        st.read_s = mx[0], st.h2d_s = mx[1], st.kernel_s = mx[2], st.d2h_s = mx[3], st.write_s = mx[4];
        if (!rc) stats_report(&st, "mpi", c.nranks, c.buffer);
    }
    for (int i = 0; i < nbuf; i++) {
        free(full_in[i]);
        free(full_out[i]);
        free(loc_in[i]);
        free(loc_out[i]);
    }
    free(cnt);
    free(dsp);
    free(c.weights);
    MPI_Finalize();
    return rc;
}
