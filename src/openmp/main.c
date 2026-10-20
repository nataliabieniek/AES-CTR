/* OpenMP: watki dostaja rozlaczne zakresy blokow wraz z licznikami.
 * Rownoleglosc obliczen (OpenMP) jest oddzielona od buforowania pliku (watek I/O).
 *
 * Wersja 1 osoby: synchronicznie  read -> compute -> write.
 * Wersja 2 osob : podwojne buforowanie - podczas liczenia bufora k czytamy bufor k+1
 *                 i zapisujemy bufor k-1. */
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include "../common/aes_ctr.h"
#include "../common/cli.h"
#include "../common/fileio.h"
#include "../common/timer.h"

/* Szyfruje bufor rownolegle; first_block = globalny numer pierwszego bloku bufora
 * (offset_w_pliku / 16). Bufor (poza ostatnim) ma dlugosc podzielna przez 16. */
static void process_buffer(const aes_key *key, const cli_args *a, uint64_t first_block,
                           const uint8_t *in, uint8_t *out, size_t n) {
    /* TODO 1: nblk = (n + 15) / 16
     * TODO 2: #pragma omp parallel for schedule(static) po blokach (albo: kazdy watek
     *         dostaje jeden ciagly zakres blokow = rozlaczne zakresy licznikow)
     * TODO 3: w petli wywolaj aes_ctr_xcrypt(key, a->counter, first_block + b_start,
     *         in + b_start*16, out + b_start*16, len) - len < 16 tylko dla ostatniego bloku
     * UWAGA: tu NIE mierz czasu i NIE printuj - czas mierzy main wokol wywolania. */
    (void)key; (void)a; (void)first_block; (void)in; (void)out; (void)n;
}

int main(int argc, char **argv) {
    cli_args a;
    if (cli_parse(argc, argv, &a)) return 1;
    run_stats st;
    stats_reset(&st);

    /* KROK 1: konfiguracja (poza pomiarem)
     *   - omp_set_num_threads(a.threads) gdy a.threads > 0
     *   - aes_key_init(&key, a.key, a.key_len)
     *   - size = file_size(a.in_path); cli_check_counter_range(&a, (size + 15) / 16)
     *   - file_open wejscia ("rb") i wyjscia ("wb")
     *   - malloc buforow: wersja 1 osoby: in[1], out[1];
     *     wersja 2 osob: in[2], out[2] (kazdy a.buffer_size)
     */

    /* KROK 2: start pomiaru calego potoku: t_total = now_sec()  (tuz przed 1. odczytem) */

    /* KROK 3A (1 osoba, a.double_buffer == 0, synchronicznie), petla po buforach, off = offset w pliku:
     *   t = now_sec(); n = file_read_at(fi, off, in, buffer); st.read_s += now_sec() - t;
     *   if (n == 0) break;
     *   t = now_sec(); process_buffer(&key, &a, off/16, in, out, n); st.kernel_s += now_sec() - t;
     *   t = now_sec(); file_write_at(fo, off, out, n); st.write_s += now_sec() - t;
     *   off += n; st.bytes += n;
     */

    /* KROK 3B (2 osoby, a.double_buffer == 1, podwojne buforowanie):
     *   - #pragma omp parallel sections (lub 2 taski): jedna sekcja = I/O, druga = obliczenia
     *   - indeks bufora k%2: w czasie gdy process_buffer liczy bufor k,
     *       watek I/O czyta bufor k+1 do drugiego bufora wejsciowego
     *       oraz zapisuje wynik bufora k-1 z drugiego bufora wyjsciowego
     *   - synchronizacja na granicy buforow (#pragma omp barrier / zmienne gotowosci)
     *   - st.kernel_s nadal = suma czasow process_buffer; total_s bedzie krotszy niz
     *     read+kernel+write - to efekt nakladania
     */

    /* KROK 4: koniec potoku: st.total_s = now_sec() - t_total;
     *         dopiero TERAZ zamknij pliki, zwolnij pamiec i wypisz:
     *         stats_report(&st, "openmp", omp_get_max_threads(), a.buffer_size);
     */
    /* KROK 5: DESZYFROWANIE = ten sam program (CTR jest symetryczny): uruchom z --in zaszyfrowany
     *         --out odszyfrowany, ten sam klucz i --counter. Brak osobnego kodu; w main ewentualnie
     *         tylko komunikat. Weryfikacja bajt po bajcie: python/verify.py (porownanie z oryginalem).
     */
    (void)process_buffer;
    return 0;
}
