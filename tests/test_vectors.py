import json
from pathlib import Path

import pytest
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

VECTORS = json.loads((Path(__file__).parent.parent / "data" / "vectors" / "aes-ctr.json").read_text())


def ref_ctr(key: bytes, counter: bytes, data: bytes) -> bytes:
    return Cipher(algorithms.AES(key), modes.CTR(counter)).encryptor().update(data)


@pytest.mark.parametrize("v", VECTORS, ids=[v["name"] for v in VECTORS])
def test_reference_matches_vector(v):
    key, ctr = bytes.fromhex(v["key"]), bytes.fromhex(v["counter"])
    pt, ct = bytes.fromhex(v["plaintext"]), bytes.fromhex(v["ciphertext"])
    assert ref_ctr(key, ctr, pt) == ct
    assert ref_ctr(key, ctr, ct) == pt
