# AES-CTR – równoległe szyfrowanie plików (PRiR, projekt C01)

Równoległe szyfrowanie/deszyfrowanie plików AES-CTR w trzech wersjach: **OpenMP**, **MPI**, **CUDA**, ze wspólnym kodem w `src/common/`. Wszystko uruchamiamy w Dockerze (CUDA 12.4 + Ubuntu 22.04, gcc, OpenMPI, Python 3).

## 1. Stan projektu

| Element | Stan |
|---|---|
| `src/common/` (AES-128/192/256, CLI, I/O, czas, GB/s) | gotowe, zweryfikowane z `cryptography` |
| `src/cuda/main.cu` | gotowe: tryb pojedynczy i podwójne buforowanie (`--double 1`) |
| `src/openmp/main.c` | tryb pojedynczy gotowy; **`--double 1` do zrobienia** (na razie liczy synchronicznie) |
| `src/mpi/main.c` | **szablon z TODO** (do zrobienia) |
| Dane testowe, wektory NIST/RFC, `verify.py`, skrypt `run_all.sh` | gotowe |
| Benchmarki (wpływ rozmiaru bufora i pliku), raport | do zrobienia |

## 2. Struktura
- `src/common/` – wspólny kod: `aes_ctr.c` (AES-CTR), `cli.c` (argumenty), `fileio.c` (I/O z offsetem), `timer.c` (czas i raport GB/s)
- `src/openmp/`, `src/mpi/`, `src/cuda/` – tylko kod równoległości danej wersji
- `scripts/run_all.sh` – uruchamia wersje na wszystkich danych i sprawdza poprawność
- `python/` – `gen_data.py` (dane), `verify.py` (weryfikacja z biblioteką)
- `tests/` – testy środowiska i wektorów testowych
- `data/` – pliki wejściowe (`*.bin` w `.gitignore`) i wektory `data/vectors/aes-ctr.json`
- `results/` – wyniki (`run.txt` dopisuje `run_all.sh`); `report/` – raport
- `Makefile` – buduje do `build/`

## 3. Pierwsze uruchomienie

### Wariant A: Docker (zalecany – pełne środowisko)
Wymagania: uruchomiony Docker Desktop, sterownik NVIDIA z obsługą GPU w Dockerze (WSL2).
```
docker compose build                  # tylko za pierwszym razem (kilka–kilkanaście minut)
docker compose run --rm dev           # powłoka w kontenerze (/workspace = ten folder)
```
Dalsze komendy w tym README (`python3`, `make`, `bash`, `mpirun`) wpisujemy **w tej powłoce kontenera**, nie w PowerShellu.
```
python3 python/gen_data.py            # generuje 5 plików testowych w data/
pytest tests                          # test środowiska + wektory testowe
make                                  # buduje openmp, mpi i cuda do build/
```

### Wariant B: lokalnie w Pythonie (PowerShell, bez Dockera)
Wystarczy do danych, wektorów testowych i `verify.py`. OpenMP, MPI i CUDA wymagają Dockera.
```
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install numpy cryptography pytest matplotlib
python python/gen_data.py
pytest tests/test_vectors.py
```

## 4. Uruchamianie – od najprostszego

### 4.1. Jedna komenda: wszystko na wszystkich danych
W kontenerze (buduje, uruchamia, deszyfruje i sprawdza każdy plik):
```
bash scripts/run_all.sh
```
Wypisuje po każdym uruchomieniu linię z wynikami, `round-trip OK` (deszyfrowanie odtwarza plik) i `OK` z `verify.py` (zgodność z biblioteką). Wyniki są dopisywane do `results/run.txt`.

Opcje (wszystkie opcjonalne):

| Flaga | Znaczenie | Domyślnie |
|---|---|---|
| `-v openmp\|mpi\|cuda\|all` | wersja | `all` |
| `-d 0\|1\|both` | buforowanie: 0 pojedyncze, 1 podwójne, `both` oba | `both` |
| `-b BAJTY` | rozmiar bufora (wielokrotność 16) | `1048576` |
| `-t N` | wątki OpenMP / procesy MPI | `4` |
| `-f WZORZEC` | pliki wejściowe | `data/file_*.bin` |

Przykłady:
```
bash scripts/run_all.sh -v cuda -d both            # CUDA: pojedyncze vs podwójne
bash scripts/run_all.sh -v openmp -d 0 -t 8        # OpenMP, 8 wątków
bash scripts/run_all.sh -v mpi -t 4                # MPI, 4 procesy
bash scripts/run_all.sh -b 65536 -f data/file_100MB.bin   # inny bufor, jeden plik
```
Uwaga: dopóki OpenMP nie ma podwójnego buforowania, `-d 1` daje dla niego to samo co `-d 0` (program wypisuje ostrzeżenie); MPI jest szablonem i dopóki nie zostanie zaimplementowane, jego wyniki są nieprawidłowe.

### 4.2. Pojedyncze polecenia
Klucz i licznik poniżej są przykładowe.
```
K=2b7e151628aed2a6abf7158809cf4f3c; C=f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff

./build/aes_ctr_openmp --in data/file_10MB.bin --out results/10MB.enc --key $K --counter $C --buffer 1048576 --threads 4
mpirun --allow-run-as-root -n 4 ./build/aes_ctr_mpi --in data/file_10MB.bin --out results/10MB.enc --key $K --counter $C
./build/aes_ctr_cuda --in data/file_10MB.bin --out results/10MB.enc --key $K --counter $C --buffer 16777216
```
Podwójne buforowanie (zakres 2 osób): dodaj `--double 1`.

Deszyfrowanie = to samo polecenie na pliku zaszyfrowanym (CTR jest symetryczny):
```
./build/aes_ctr_cuda --in results/10MB.enc --out results/10MB.dec --key $K --counter $C
```
Weryfikacja:
```
python3 python/verify.py enc data/file_10MB.bin results/10MB.enc $K $C    # zgodność z biblioteką
python3 python/verify.py dec data/file_10MB.bin results/10MB.dec $K $C    # odzyskanie pliku
```

### 4.3. Argumenty programów
Wszystkie wersje mają te same argumenty:
```
aes_ctr_<wersja> --in PLIK --out PLIK --key HEX(32|48|64) --counter HEX(32) [--buffer BAJTY=1048576] [--threads N] [--double 0|1]
```
- `--key`: AES-128/192/256; `--counter`: początkowy 128-bitowy blok licznika; `--buffer`: wielokrotność 16.
- `--double`: 0 = synchroniczne bufory (zakres 1 osoby), 1 = podwójne buforowanie (zakres 2 osób).
- Blok `i` używa licznika `counter + i` (big-endian); program odrzuca plik, przy którym licznik by się zawinął. Ostatni niepełny blok jest obsługiwany.

### 4.4. Wynik programu
Jedna linia `klucz=wartość`: czasy faz (`read_s`, `h2d_s`, `kernel_s`, `d2h_s`, `write_s`, `total_s`), `kernel_GBps` (sam rdzeń) i `total_GBps` (cały potok). Pomiar jest tylko akumulowany w `run_stats` i wypisywany po zakończeniu pracy, więc nie zaburza szyfrowania. Przy podwójnym buforowaniu `total_s` jest mniejsze niż suma faz (fazy się nakładają).

## 5. Dla piszących kod

### Kolejność wywołań funkcji z `src/common/`
```
cli_parse(argc, argv, &a)                       // argumenty
aes_key_init(&key, a.key, a.key_len)            // klucz (raz, przed częścią równoległą)
size = file_size(a.in_path); cli_check_counter_range(&a, (size+15)/16)
file_open / file_read_at / file_write_at        // I/O z offsetem
aes_ctr_xcrypt(&key, a.counter, first_block, in, out, len)   // first_block = offset/16
stats_reset(&st); st.kernel_s += ...; stats_report(&st, "openmp", workers, a.buffer_size)
```
Opisy funkcji są w nagłówkach `src/common/*.h`. Kroki 1–5 (konfiguracja, start pomiaru, pętla po partiach, koniec i raport, deszyfrowanie) opisują komentarze w `main` każdej wersji; `src/cuda/main.cu` jest działającym wzorcem.


## 6. Wektory testowe
`data/vectors/aes-ctr.json` zawiera wektory AES-128 CTR (klucz, początkowy blok licznika, plaintext, ciphertext, hex):
- NIST SP 800-38A, F.5.1 (4 bloki),
- RFC 3686, Test Vector #1 i #2 (blok licznika = nonce ‖ IV ‖ `00000001`).

`tests/test_vectors.py` sprawdza na nich implementację referencyjną; kolejne wersje mają być testowane tymi samymi wektorami.

## 7. Dane testowe (`data/`)
`python3 python/gen_data.py` generuje 5 plików (ustalony seed): `file_15KB.bin` (15 KiB + 5 B, test niepełnego bloku), `file_100KB.bin`, `file_1MB.bin`, `file_10MB.bin`, `file_100MB.bin`.
