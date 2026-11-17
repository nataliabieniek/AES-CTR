"""Poprawnosc build/aes_ctr_mpi (najpierw `make mpi`; wymaga mpirun)."""
import json
import shutil
import subprocess
from pathlib import Path

import pytest
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "aes_ctr_mpi"
VECTORS = json.loads((ROOT / "data" / "vectors" / "aes-ctr.json").read_text())
KEY = "2b7e151628aed2a6abf7158809cf4f3c"
CTR = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"

pytestmark = pytest.mark.skipif(not BIN.exists() or not shutil.which("mpirun"),
                                reason="brak build/aes_ctr_mpi lub mpirun")


def run(tmp_path, data, key=KEY, ctr=CTR, buffer=1 << 20, procs=4, double=0, weights=None):
    src, dst = tmp_path / "in.bin", tmp_path / "out.bin"
    src.write_bytes(data)
    cmd = ["mpirun", "--allow-run-as-root", "--oversubscribe", "-n", str(procs)]
    if weights:
        cmd += ["-x", f"AES_MPI_WEIGHTS={weights}"]
    cmd += [str(BIN), "--in", str(src), "--out", str(dst), "--key", key, "--counter", ctr,
            "--buffer", str(buffer), "--double", str(double)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc, (dst.read_bytes() if dst.exists() else b"")


def ref(data, key=KEY, ctr=CTR):
    return Cipher(algorithms.AES(bytes.fromhex(key)), modes.CTR(bytes.fromhex(ctr))).encryptor().update(data)


def pattern(n):
    return bytes((i * 131 + 7) % 256 for i in range(n))


@pytest.mark.parametrize("double", [0, 1])
@pytest.mark.parametrize("v", VECTORS, ids=[v["name"] for v in VECTORS])
def test_published_vectors(tmp_path, v, double):
    proc, out = run(tmp_path, bytes.fromhex(v["plaintext"]), v["key"], v["counter"], double=double)
    assert proc.returncode == 0, proc.stderr
    assert out.hex() == v["ciphertext"]


# 0: pusty plik; 1, 15, 17: niepelny blok; 4096 + 5: niepelny blok po kilku partiach;
# wiecej procesow niz blokow w partii (16 B * 8 procesow)
@pytest.mark.parametrize("double", [0, 1])
@pytest.mark.parametrize("procs", [1, 3, 8])
@pytest.mark.parametrize("buffer", [16, 1024, 1 << 20])
@pytest.mark.parametrize("size", [0, 1, 15, 16, 17, 4096 + 5, 300_000])
def test_matches_library_and_round_trips(tmp_path, size, buffer, procs, double):
    data = pattern(size)
    proc, enc = run(tmp_path, data, buffer=buffer, procs=procs, double=double)
    assert proc.returncode == 0, proc.stderr
    assert enc == ref(data)
    proc, dec = run(tmp_path, enc, buffer=buffer, procs=procs, double=double)
    assert proc.returncode == 0, proc.stderr
    assert dec == data


@pytest.mark.parametrize("double", [0, 1])
def test_uneven_weights(tmp_path, double):
    data = pattern(100_003)
    proc, enc = run(tmp_path, data, buffer=4096, procs=3, double=double, weights="6,1,2")
    assert proc.returncode == 0, proc.stderr
    assert enc == ref(data)


def test_wrong_number_of_weights_is_rejected(tmp_path):
    proc, _ = run(tmp_path, pattern(64), procs=3, weights="1,2")
    assert proc.returncode != 0


def test_counter_wraparound_is_rejected(tmp_path):
    proc, _ = run(tmp_path, pattern(32), ctr="ff" * 16)
    assert proc.returncode != 0
