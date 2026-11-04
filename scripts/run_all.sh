#!/usr/bin/env bash
# Uruchamia wersje na wszystkich plikach z data/, szyfruje, deszyfruje i sprawdza poprawnosc.
# Uzycie (w kontenerze): bash scripts/run_all.sh [-v openmp|mpi|cuda|all] [-d 0|1|both] [-b BUFOR] [-t N] [-f WZORZEC]
#   -v  wersja            (domyslnie all = openmp mpi cuda)
#   -d  buforowanie       0 = pojedyncze, 1 = podwojne, both = oba (domyslnie both)
#   -b  rozmiar bufora B  (domyslnie 1048576)
#   -t  watki OpenMP / procesy MPI (domyslnie 4)
#   -f  wzorzec plikow    (domyslnie 'data/file_*.bin', np. -f data/file_1MB.bin)
V=all; D=both; BUF=1048576; THR=4; PAT='data/file_*.bin'
while getopts "v:d:b:t:f:" o; do
  case $o in
    v) V=$OPTARG;; d) D=$OPTARG;; b) BUF=$OPTARG;; t) THR=$OPTARG;; f) PAT=$OPTARG;;
    *) sed -n '2,8p' "$0"; exit 1;;
  esac
done
[ "$V" = all ] && VARS="openmp mpi cuda" || VARS=$V
[ "$D" = both ] && DBLS="0 1" || DBLS=$D

K=000102030405060708090a0b0c0d0e0f
mkdir -p results build
make >/dev/null || exit 1
[ -f data/file_100MB.bin ] || python3 python/gen_data.py

run() { # run WERSJA IN OUT DOUBLE
  local args="--in $2 --out $3 --key $K --counter $K --buffer $BUF --threads $THR --double $4"
  if [ "$1" = mpi ]; then
    mpirun --allow-run-as-root --oversubscribe -n "$THR" ./build/aes_ctr_mpi $args
  else
    ./build/aes_ctr_$1 $args
  fi
}

for f in $PAT; do
  for v in $VARS; do
    for d in $DBLS; do
      echo "== $v double=$d $f"
      run $v "$f" /tmp/enc.bin $d 2>&1 | tee -a results/run.txt
      run $v /tmp/enc.bin /tmp/dec.bin $d > /dev/null 2>&1
      cmp /tmp/dec.bin "$f" && echo "round-trip OK" || echo "round-trip BLAD"
      python3 python/verify.py enc "$f" /tmp/enc.bin $K $K || true
    done
  done
done
