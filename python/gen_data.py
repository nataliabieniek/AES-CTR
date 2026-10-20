"""Generuje pliki testowe w data/ (pseudolosowe, ustalony seed => powtarzalne)."""
import random
from pathlib import Path

DATA = Path(__file__).resolve().parent.parent / "data"
SEED = 12345
# ostatni niepelny blok (rozmiar % 16 != 0) w pierwszym pliku testuje obsluge reszty
FILES = {
    "file_15KB.bin": 15 * 1024 + 5,
    "file_100KB.bin": 100 * 1024,
    "file_1MB.bin": 1024 * 1024,
    "file_10MB.bin": 10 * 1024 * 1024,
    "file_100MB.bin": 100 * 1024 * 1024,
}

if __name__ == "__main__":
    for name, size in FILES.items():
        rng = random.Random(SEED + size)
        (DATA / name).write_bytes(rng.randbytes(size))
        print(f"{name}: {size} B")
