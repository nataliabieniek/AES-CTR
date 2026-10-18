# AES-CTR – równoległe szyfrowanie plików (PRiR, projekt C01)

Środowisko: Docker (CUDA 12.4 + Ubuntu 22.04), Python 3, OpenMP (gcc), MPI (OpenMPI + mpi4py), CUDA (nvcc + pycuda), `cryptography` jako implementacja referencyjna.

## Pierwsze uruchomienie

### Wariant A: Docker (zalecany – pełne środowisko OpenMP + MPI + CUDA)
Wymagania: Docker Desktop (uruchomiony), sterownik NVIDIA z obsługą GPU w Dockerze (WSL2).
```
docker compose build                  # pierwszy raz kilka–kilkanaście minut
docker compose run --rm dev           # powłoka w kontenerze (/workspace = ten folder)
pytest tests                          # wewnątrz kontenera: test środowiska + wektory
```

## Budowanie i uruchamianie kodu (w kontenerze)
```
make openmp && ./build/aes_ctr_openmp --in data/small.bin --out results/small.enc \
    --key 2b7e151628aed2a6abf7158809cf4f3c --counter f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff \
    --buffer 1048576 --threads 4

make mpi && mpirun -n 4 ./build/aes_ctr_mpi --in data/small.bin --out results/small.enc \
    --key 2b7e151628aed2a6abf7158809cf4f3c --counter f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff

make cuda && ./build/aes_ctr_cuda --in data/small.bin --out results/small.enc \
    --key 2b7e151628aed2a6abf7158809cf4f3c --counter f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff \
    --buffer 16777216
```
Deszyfrowanie: to samo polecenie na pliku zaszyfrowanym (z tym samym kluczem i licznikiem).
Weryfikacja: porównaj wynik z `cryptography` (`python/`) lub wektorami z `data/vectors/`.

Stan kodu: to **szablon**. Pliki w `src/` zawierają deklaracje i `TODO`; do czasu implementacji programy nic nie szyfrują.
Kolejność prac: `src/common/aes_ctr.c` → `src/common/cli.c` → `src/openmp/main.c` → `src/mpi/main.c` → `src/cuda/main.cu`.

## Wektory testowe
`data/vectors/aes-ctr.json` zawiera opublikowane wektory AES-128 CTR (klucz, początkowy blok licznika, plaintext, ciphertext, w hex):
- NIST SP 800-38A, F.5.1 (4 bloki),
- RFC 3686, Test Vector #1 i #2 (blok licznika = nonce ‖ IV ‖ `00000001`).

`tests/test_vectors.py` sprawdza na nich implementację referencyjną (`cryptography`); kolejne implementacje (OpenMP, MPI, CUDA) mają być testowane tymi samymi wektorami.

## Interfejs programów
Wszystkie wersje mają te same argumenty (z treści zadania: plik, klucz, licznik, bufor):
```
aes_ctr_<wersja> --in PLIK --out PLIK --key HEX(32|48|64) --counter HEX(32) [--buffer BAJTY=1048576] [--threads N]
```
- `--key`: AES-128/192/256, `--counter`: początkowy 128-bitowy blok licznika, `--buffer`: wielokrotność 16.
- Blok o numerze `i` używa licznika `counter + i` (big-endian); program odrzuca plik, przy którym licznik by się zawinął (brak powtórzeń strumienia klucza).
- Ostatni niepełny blok jest obsługiwany. Szyfrowanie = deszyfrowanie (ten sam polecenie).
- Wyjście: jedna linia z czasem jądra i całego potoku oraz GB/s.

## Struktura
- `src/common/` – wspólny kod: AES (`aes_ctr.c`), licznik 128-bit, parser argumentów i pomiar czasu (`cli.c`)
- `src/openmp/`, `src/mpi/`, `src/cuda/` – tylko kod równoległości (szablony z TODO)
- `Makefile` – `make openmp | mpi | cuda` (w Dockerze), binaria w `build/`
- `python/` – narzędzia, benchmarki, wrapper referencyjny
- `tests/` – testy (wektory NIST AES-CTR, zgodność z biblioteką)
- `data/`, `results/` – pliki wejściowe i wyniki pomiarów
- `report/` – raport
