#ifndef FILEIO_H
#define FILEIO_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Rozmiar pliku w bajtach; -1 przy bledzie. */
int64_t file_size(const char *path);

/* Otwiera plik ("rb"/"wb"); NULL + komunikat przy bledzie. */
FILE *file_open(const char *path, const char *mode);

/* Czyta maks. n bajtow od offsetu; zwraca liczbe przeczytanych (0 = koniec/blad). */
size_t file_read_at(FILE *f, uint64_t offset, uint8_t *buf, size_t n);

/* Zapisuje n bajtow od offsetu; zwraca liczbe zapisanych. */
size_t file_write_at(FILE *f, uint64_t offset, const uint8_t *buf, size_t n);

/* Wczytuje caly plik do pamieci (malloc; zwolnij free). NULL przy bledzie. */
uint8_t *file_read_all(const char *path, size_t *len);

/* Zapisuje caly bufor do pliku. 0 = OK. */
int file_write_all(const char *path, const uint8_t *buf, size_t len);

#endif
