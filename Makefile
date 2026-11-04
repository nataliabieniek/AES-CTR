CC      ?= gcc
MPICC   ?= mpicc
NVCC    ?= nvcc
CFLAGS  ?= -O3 -std=gnu11 -Wall -Wextra
COMMON  := src/common/aes_ctr.c src/common/cli.c src/common/timer.c src/common/fileio.c
BIN     := build
COMMON_OBJ := $(patsubst src/common/%.c,$(BIN)/common_%.o,$(COMMON))

all: openmp mpi cuda

$(BIN):
	mkdir -p $(BIN)

# wspolny kod C kompilowany gcc (nvcc traktuje .c jako C++)
$(BIN)/common_%.o: src/common/%.c | $(BIN)
	$(CC) $(CFLAGS) -c $< -o $@

openmp: $(BIN)
	$(CC) $(CFLAGS) -fopenmp src/openmp/main.c $(COMMON) -o $(BIN)/aes_ctr_openmp

mpi: $(BIN)
	$(MPICC) $(CFLAGS) src/mpi/main.c $(COMMON) -o $(BIN)/aes_ctr_mpi

cuda: $(COMMON_OBJ) | $(BIN)
	$(NVCC) -O3 -arch=native src/cuda/main.cu $(COMMON_OBJ) -o $(BIN)/aes_ctr_cuda

clean:
	rm -rf $(BIN)

.PHONY: all openmp mpi cuda clean
