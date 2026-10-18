CC      ?= gcc
MPICC   ?= mpicc
NVCC    ?= nvcc
CFLAGS  ?= -O3 -std=c11 -Wall -Wextra
COMMON  := src/common/aes_ctr.c src/common/cli.c
BIN     := build

all: openmp mpi cuda

$(BIN):
	mkdir -p $(BIN)

openmp: $(BIN)
	$(CC) $(CFLAGS) -fopenmp src/openmp/main.c $(COMMON) -o $(BIN)/aes_ctr_openmp

mpi: $(BIN)
	$(MPICC) $(CFLAGS) src/mpi/main.c $(COMMON) -o $(BIN)/aes_ctr_mpi

cuda: $(BIN)
	$(NVCC) -O3 src/cuda/main.cu src/common/aes_ctr.c src/common/cli.c -o $(BIN)/aes_ctr_cuda

clean:
	rm -rf $(BIN)

.PHONY: all openmp mpi cuda clean
