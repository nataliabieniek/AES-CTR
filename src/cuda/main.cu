// CUDA: watek = jeden blok AES; licznik = base + globalny indeks bloku (pierwszy blok partii
// + blockIdx.x*blockDim.x + threadIdx.x). Plik przetwarzany w PARTIACH (--buffer).
// Raportujemy transfery H2D i D2H osobno od jadra.
//
// Wersja 1 osoby: synchronicznie  read -> H2D -> kernel -> D2H -> write.
// Wersja 2 osob : podwojne buforowanie na 2 strumieniach (pinned memory), nakladanie
//                 transferow na obliczenia.
#include <cstdio>
#include <cstdlib>
#include <cuda_runtime.h>
extern "C" {
#include "../common/aes_ctr.h"
#include "../common/cli.h"
#include "../common/fileio.h"
#include "../common/timer.h"
}

__global__ void aes_ctr_kernel(const uint8_t *d_rk, int rounds, const uint8_t *d_base,
                               uint64_t first_block, const uint8_t *in, uint8_t *out,
                               size_t len) {
    // TODO 1: idx = blockIdx.x * blockDim.x + threadIdx.x; offset = idx * 16; if (offset >= len) return;
    // TODO 2: licznik = d_base + (first_block + idx) - 128-bit z przenoszeniem (wersja __device__ ctr_add)
    // TODO 3: AES na urzadzeniu: S-box i klucze rund w __constant__ / __shared__ memory
    // TODO 4: out = in XOR keystream; ostatni niepelny blok: tylko len - offset bajtow
}

int main(int argc, char **argv) {
    cli_args a;
    if (cli_parse(argc, argv, &a)) return 1;
    run_stats st;
    stats_reset(&st);

    // KROK 1: konfiguracja (poza pomiarem)
    //   - aes_key_init; size = file_size; cli_check_counter_range; otwarcie plikow
    //   - cudaMalloc: d_in[2], d_out[2] (po buffer_size), klucze rund + S-box
    //     (cudaMemcpyToSymbol lub cudaMemcpy), base licznika
    //   - cudaMallocHost (pinned) h_in[2], h_out[2] -> konieczne do prawdziwego async
    //   - cudaStreamCreate x2 (wersja 2 osob), cudaEventCreate do pomiaru
    //   - rozgrzewka: jedno uruchomienie kernela na pustych danych (poza pomiarem)

    // KROK 2: t_total = now_sec()  (tuz przed pierwszym odczytem)

    // KROK 3A (1 osoba, a.double_buffer == 0), petla po partiach (offset off, n = bajty partii):
    //   t=now_sec(); n = file_read_at(fi, off, h_in, buffer); st.read_s += now_sec()-t; if (!n) break;
    //   cudaEventRecord(e0); cudaMemcpy H2D; cudaEventRecord(e1);
    //   aes_ctr_kernel<<<grid, 256>>>(..., off/16, ...);              cudaEventRecord(e2);
    //   cudaMemcpy D2H;                                               cudaEventRecord(e3);
    //   cudaEventSynchronize(e3) -> cudaEventElapsedTime: h2d = e0..e1, kernel = e1..e2,
    //   d2h = e2..e3 (ms/1000) -> dodaj do st.h2d_s / st.kernel_s / st.d2h_s
    //   t=now_sec(); file_write_at(fo, off, h_out, n); st.write_s += now_sec()-t;
    //   st.bytes += n; off += n;
    //   Zdarzenia CUDA mierza dokladnie jadro i transfery; NIE dawaj cudaDeviceSynchronize
    //   tylko dla pomiaru (psuloby nakladanie w wersji 2 osob).

    // KROK 3B (2 osoby, a.double_buffer == 1, podwojne buforowanie): partia k na strumieniu k%2:
    //   cudaMemcpyAsync H2D -> kernel -> cudaMemcpyAsync D2H (wszystko w tym strumieniu);
    //   gdy strumien k%2 liczy, host czyta partie k+1 i zapisuje wynik partii k-1
    //   (cudaStreamSynchronize / cudaEventSynchronize na strumieniu k-1 przed zapisem).
    //   Zdarzenia przed/po kazdym etapie kazdej partii; sumuj czasy. total_s < suma faz.

    // KROK 4: cudaDeviceSynchronize(); st.total_s = now_sec() - t_total; dopiero potem
    //         zamknij pliki, zwolnij pamiec/strumienie/zdarzenia i wypisz:
    //         stats_report(&st, "cuda", 1, a.buffer_size);
    // KROK 5: DESZYFROWANIE = ten sam program (CTR jest symetryczny): --in zaszyfrowany,
    //         --out odszyfrowany, ten sam klucz i --counter. Weryfikacja: python/verify.py.
    return 0;
