import subprocess


def test_cryptography_aes_ctr():
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    c = Cipher(algorithms.AES(bytes(16)), modes.CTR(bytes(16))).encryptor()
    assert len(c.update(b"x" * 32)) == 32


def test_mpi4py():
    from mpi4py import MPI
    assert MPI.COMM_WORLD.Get_size() >= 1


def test_openmp_compiler():
    out = subprocess.run(["gcc", "-fopenmp", "--version"], capture_output=True)
    assert out.returncode == 0


def test_nvcc_and_pycuda():
    assert subprocess.run(["nvcc", "--version"], capture_output=True).returncode == 0
    import pycuda.autoinit  # noqa: F401
