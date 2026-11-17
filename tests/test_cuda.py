"""Poprawnosc build/aes_ctr_cuda (najpierw `make cuda`, wymaga GPU NVIDIA)."""
import json
import subprocess
from pathlib import Path

import pytest
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parent.parent
BIN = ROOT / "build" / "aes_ctr_cuda"
VECTORS = json.loads((ROOT / "data" / "vectors" / "aes-ctr.json").read_text())
KEY = "2b7e151628aed2a6abf7158809cf4f3c"
CTR = "f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"

pytestmark = pytest.mark.skipif(not BIN.exists(), reason="brak build/aes_ctr_cuda")


def run(tmp_path, data, key=KEY, ctr=CTR, buffer=1 << 20, double=0):
    src, dst = tmp_path / "in.bin", tmp_path / "out.bin"
    src.write_bytes(data)
    cmd = [str(BIN), "--in", str(src), "--out", str(dst), "--key", key, "--counter", ctr,
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


# 0: pusty plik; 1, 15, 17: niepelny blok; 4096 + 5: niepelny blok po kilku partiach
@pytest.mark.parametrize("double", [0, 1])
@pytest.mark.parametrize("buffer", [16, 1024, 1 << 20])
@pytest.mark.parametrize("size", [0, 1, 15, 16, 17, 4096 + 5, 300_000])
def test_matches_library_and_round_trips(tmp_path, size, buffer, double):
    data = pattern(size)
    proc, enc = run(tmp_path, data, buffer=buffer, double=double)
    assert proc.returncode == 0, proc.stderr
    assert enc == ref(data)
    proc, dec = run(tmp_path, enc, buffer=buffer, double=double)
    assert proc.returncode == 0, proc.stderr
    assert dec == data


@pytest.mark.parametrize("key", ["00" * 24, "00" * 32], ids=["aes192", "aes256"])
def test_longer_keys(tmp_path, key):
    data = pattern(5000)
    proc, enc = run(tmp_path, data, key=key, double=1, buffer=1024)
    assert proc.returncode == 0, proc.stderr
    assert enc == ref(data, key=key)


def test_counter_carry_across_bytes(tmp_path):
    # przeniesienie przez kilka bajtow licznika w srodku pliku
    ctr = "00" * 8 + "ffffffff" + "fffffff0"
    data = pattern(2000)
    proc, enc = run(tmp_path, data, ctr=ctr, buffer=256, double=1)
    assert proc.returncode == 0, proc.stderr
    assert enc == ref(data, ctr=ctr)


def test_counter_wraparound_is_rejected(tmp_path):
    # 2 bloki od licznika 2^128 - 1: drugi dostalby licznik 0
    proc, _ = run(tmp_path, pattern(32), ctr="ff" * 16)
    assert proc.returncode != 0
