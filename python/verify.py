"""Weryfikacja: python python/verify.py enc|dec ORYGINAL PLIK KLUCZ_HEX LICZNIK_HEX
enc: PLIK == AES-CTR(ORYGINAL) (zgodnosc z biblioteka); dec: PLIK == ORYGINAL (odzyskanie bajt po bajcie)."""
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def main(mode, orig, out, key, ctr):
    data = open(orig, "rb").read()
    got = open(out, "rb").read()
    if mode == "enc":
        want = Cipher(algorithms.AES(bytes.fromhex(key)), modes.CTR(bytes.fromhex(ctr))).encryptor().update(data)
    else:
        want = data
    ok = got == want
    print("OK" if ok else "ROZNICA", len(got), "B")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 6 or sys.argv[1] not in ("enc", "dec"):
        sys.exit(__doc__)
    sys.exit(main(*sys.argv[1:]))
