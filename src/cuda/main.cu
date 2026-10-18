// CUDA: watek = jeden blok AES, licznik = base + globalny indeks bloku.
// Pamietaj o transferach H2D/D2H i o wielkosci partii (--buffer).
#include <cstdio>
#include <cuda_runtime.h>
extern "C" {
#include "../common/aes_ctr.h"
#include "../common/cli.h"
}

__global__ void aes_ctr_kernel(const uint8_t *round_keys, int rounds, const uint8_t *base,
                               uint64_t first_block, const uint8_t *in, uint8_t *out,
                               size_t len) {
    // TODO: indeks bloku z threadIdx/blockIdx, licznik = base + first_block + idx,
    // AES na urzadzeniu (S-box w pamieci stalej/wspolnej), XOR, ostatni niepelny blok
}

int main(int argc, char **argv) {
    cli_args a;
    if (cli_parse(argc, argv, &a)) return 1;
    // TODO: aes_key_init, alokacja bufora hosta (pinned) i urzadzenia
    // TODO: petla po partiach: cudaMemcpyAsync H2D -> kernel -> D2H -> fwrite
    // TODO: pomiary cudaEvent: sam kernel vs caly potok (z transferami)
    // TODO (wersja 2 osob): podwojne buforowanie na strumieniach
    return 0;
}
