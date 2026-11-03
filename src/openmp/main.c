/* AES-CTR, wersja OpenMP.
 * Watki dostaja rozlaczne zakresy blokow (a wiec i licznikow); odczyt i zapis
 * pliku robi watek glowny poza sekcja rownolegla.
 * --double 1 (podwojne buforowanie, zakres 2 osob) - TODO, na razie dziala jak 0.
 * Deszyfrowanie = ten sam program (CTR jest symetryczny). */
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/aes_ctr.h"
#include "../common/cli.h"
#include "../common/fileio.h"
#include "../common/timer.h"

/* first_block = globalny numer pierwszego bloku bufora (offset / 16).
 * schedule(static) bez chunk: kazdy watek dostaje jeden ciagly zakres blokow. */
static void process_buffer(const aes_key *key, const cli_args *a, uint64_t first_block,
                           const uint8_t *in, uint8_t *out, size_t n) {
    const int64_t nblk = (int64_t)((n + AES_BLOCK - 1) / AES_BLOCK);
#pragma omp parallel for schedule(static)
    for (int64_t b = 0; b < nblk; b++) {
        size_t off = (size_t)b * AES_BLOCK;
        size_t len = n - off < AES_BLOCK ? n - off : AES_BLOCK; /* < 16 tylko w ostatnim bloku */
        aes_ctr_xcrypt(key, a->counter, first_block + (uint64_t)b, in + off, out + off, len);
    }
}

int main(int argc, char **argv) {
    cli_args a;
    if (cli_parse(argc, argv, &a)) return 1;
    run_stats st;
    stats_reset(&st);

    /* KROK 1: konfiguracja poza pomiarem */
    if (a.threads > 0) omp_set_num_threads(a.threads);
    if (a.double_buffer)
        fprintf(stderr, "Uwaga: --double 1 jeszcze niezaimplementowane, uzywam buforow synchronicznych.\n");

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

    uint8_t *in = malloc(a.buffer_size);
    uint8_t *out = malloc(a.buffer_size);
    if (!in || !out) {
        fprintf(stderr, "Blad: brak pamieci na bufory.\n");
        free(in);
        free(out);
        fclose(fi);
        fclose(fo);
        return 1;
    }

    /* Rozgrzewka: tworzenie puli watkow nie powinno wliczac sie do kernel_s. */
#pragma omp parallel
    { (void)0; }

    /* KROK 2-3: pomiar calego potoku, synchronicznie read -> compute -> write */
    double t_total = now_sec();
    int rc = 0;
    uint64_t off = 0;
    while (off < (uint64_t)size) {
        size_t expected = (uint64_t)a.buffer_size < (uint64_t)size - off
                              ? a.buffer_size
                              : (size_t)((uint64_t)size - off);
        double t = now_sec();
        size_t n = file_read_at(fi, off, in, expected);
        st.read_s += now_sec() - t;
        if (n != expected) {
            fprintf(stderr, "Blad odczytu: oczekiwano %zu B, odczytano %zu B.\n", expected, n);
            rc = 1;
            break;
        }

        t = now_sec();
        process_buffer(&key, &a, off / AES_BLOCK, in, out, n);
        st.kernel_s += now_sec() - t;

        t = now_sec();
        size_t w = file_write_at(fo, off, out, n);
        st.write_s += now_sec() - t;
        if (w != n) {
            fprintf(stderr, "Blad zapisu: oczekiwano %zu B, zapisano %zu B.\n", n, w);
            rc = 1;
            break;
        }

        off += n;
        st.bytes += n;
    }
    /* fflush przed koncem pomiaru, zeby zapis z bufora stdio wliczyl sie do total_s */
    if (fflush(fo) != 0) {
        perror("Blad oprozniania bufora wyjsciowego");
        rc = 1;
    }
    st.total_s = now_sec() - t_total;

    /* KROK 4: sprzatanie i raport */
    if (fclose(fo) != 0) {
        perror("Blad zamykania pliku wyjsciowego");
        rc = 1;
    }
    if (fclose(fi) != 0) {
        perror("Blad zamykania pliku wejsciowego");
        rc = 1;
    }
    free(in);
    free(out);
    if (!rc && st.bytes != (uint64_t)size) {
        fprintf(stderr, "Blad: przetworzono %llu z %lld B.\n",
                st.bytes, (long long)size);
        rc = 1;
    }
    if (!rc) stats_report(&st, "openmp", omp_get_max_threads(), a.buffer_size);
    return rc;
}
