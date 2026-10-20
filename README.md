# AES-CTR – równoległe szyfrowanie plików (PRiR, projekt C01)

Środowisko: Docker (CUDA 12.4 + Ubuntu 22.04), Python 3, OpenMP (gcc), MPI (OpenMPI + mpi4py), CUDA (nvcc + pycuda), `cryptography` jako implementacja referencyjna.

## Pierwsze uruchomienie

### Wariant A: Docker (zalecany – pełne środowisko OpenMP + MPI + CUDA)
Wymagania: Docker Desktop (uruchomiony), sterownik NVIDIA z obsługą GPU w Dockerze (WSL2).
```
docker compose build                  # pierwszy raz kilka–kilkanaście minut
docker compose run --rm dev           # powłoka w kontenerze (/workspace = ten folder)
```
Wewnątrz kontenera (`requirements.txt` jest już zainstalowany przez Dockerfile):
```
python3 python/gen_data.py            # generuje 5 plików testowych w data/
pytest tests                          # test środowiska + wektory testowe
make                                  # buduje openmp, mpi i cuda do build/
```

### Wariant B: lokalnie w Pythonie (PowerShell, bez Dockera)
Wystarczy do generowania danych, wektorów testowych i `python/verify.py`. OpenMP, MPI i CUDA wymagają Dockera.
```
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install numpy cryptography pytest matplotlib    # podzbiór requirements.txt
python python/gen_data.py
pytest tests/test_vectors.py
```
(Pełne `pip install -r requirements.txt` lokalnie wymaga MPI i CUDA Toolkit dla `mpi4py`/`pycuda`.)

| Biblioteka | Budowanie | Uruchomienie |
|---|---|---|
| OpenMP | `make openmp` | `./build/aes_ctr_openmp ... --threads 4` |
| MPI | `make mpi` | `mpirun -n 4 ./build/aes_ctr_mpi ...` |
| CUDA | `make cuda` | `./build/aes_ctr_cuda ... --buffer 16777216` |
| wszystko | `make` | `make clean` czyści `build/` |

Pełne przykłady argumentów w następnej sekcji.

## Budowanie i uruchamianie kodu (w kontenerze)
Pliki wejściowe: `python python/gen_data.py` → `data/file_*.bin`. Klucz i licznik poniżej to przykładowe.
```
K=2b7e151628aed2a6abf7158809cf4f3c; C=f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff

# 1 osoba: synchroniczne bufory (--double 0, domyślnie)
make openmp && ./build/aes_ctr_openmp --in data/file_10MB.bin --out results/10MB.enc --key $K --counter $C --buffer 1048576 --threads 4
make mpi    && mpirun -n 4 ./build/aes_ctr_mpi --in data/file_10MB.bin --out results/10MB.enc --key $K --counter $C
make cuda   && ./build/aes_ctr_cuda --in data/file_10MB.bin --out results/10MB.enc --key $K --counter $C --buffer 16777216

# 2 osoby: podwójne buforowanie (nakładanie I/O i transferów) – dodaj --double 1
./build/aes_ctr_cuda --in data/file_100MB.bin --out results/100MB.enc --key $K --counter $C --buffer 16777216 --double 1

# deszyfrowanie = to samo polecenie na pliku zaszyfrowanym
./build/aes_ctr_openmp --in results/10MB.enc --out results/10MB.dec --key $K --counter $C

# weryfikacja (zgodność z biblioteką i odzyskanie pliku bajt po bajcie)
python python/verify.py enc data/file_10MB.bin results/10MB.enc $K $C
python python/verify.py dec data/file_10MB.bin results/10MB.dec $K $C
```
Wynik każdego programu to jedna linia `klucz=wartość` z `kernel_GBps` (sam rdzeń) i `total_GBps` (cały potok) oraz czasami faz. Pomiar jest tylko akumulowany w `run_stats`, a wypisywany po zakończeniu pracy, więc nie zaburza szyfrowania.

### Użycie `src/common/` w programie (kolejność wywołań)
```
cli_parse(argc, argv, &a)                       // argumenty
aes_key_init(&key, a.key, a.key_len)            // klucz (raz, przed częścią równoległą)
size = file_size(a.in_path); cli_check_counter_range(&a, (size+15)/16)
file_open / file_read_at / file_write_at        // I/O z offsetem
aes_ctr_xcrypt(&key, a.counter, first_block, in, out, len)   // first_block = offset/16
stats_reset(&st); st.kernel_s += ... ; stats_report(&st, "openmp", workers, a.buffer_size)
```
Opisy każdej funkcji są w nagłówkach `src/common/*.h`.
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
aes_ctr_<wersja> --in PLIK --out PLIK --key HEX(32|48|64) --counter HEX(32) [--buffer BAJTY=1048576] [--threads N] [--double 0|1]
```
- `--double`: 0 = synchroniczne bufory (zakres 1 osoby), 1 = podwójne buforowanie (zakres 2 osób).
- `--key`: AES-128/192/256, `--counter`: początkowy 128-bitowy blok licznika, `--buffer`: wielokrotność 16.
- Blok o numerze `i` używa licznika `counter + i` (big-endian); program odrzuca plik, przy którym licznik by się zawinął (brak powtórzeń strumienia klucza).
- Ostatni niepełny blok jest obsługiwany. Szyfrowanie = deszyfrowanie (ten sam polecenie).
- Wyjście: jedna linia z czasem jądra i całego potoku oraz GB/s.

## Dane testowe (`data/`)
`python python/gen_data.py` generuje 5 plików (ustalony seed): `file_15KB.bin` (15 KiB + 5 B, test niepełnego bloku), `file_100KB.bin`, `file_1MB.bin`, `file_10MB.bin`, `file_100MB.bin`. Pliki `*.bin` są w `.gitignore`.

## Struktura
- `src/common/` – wspólny kod: AES (`aes_ctr.c`), parser argumentów (`cli.c`), wczytywanie plików (`fileio.c`), pomiar czasu i raport GB/s (`timer.c`)
- `src/openmp/`, `src/mpi/`, `src/cuda/` – tylko kod równoległości (szablony z TODO)
- `Makefile` – `make openmp | mpi | cuda` (w Dockerze), binaria w `build/`
- `python/` – narzędzia, benchmarki, wrapper referencyjny
- `tests/` – testy (wektory NIST AES-CTR, zgodność z biblioteką)
- `data/`, `results/` – pliki wejściowe i wyniki pomiarów
- `report/` – raport
