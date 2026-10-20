#ifndef TIMER_H
#define TIMER_H

#include <stddef.h>
/* Zegar monotoniczny w sekundach. */
double now_sec(void);

typedef struct {
    double start;
    double total; /* suma zmierzonych odcinkow */
} timer_t_;

void timer_reset(timer_t_ *t);
void timer_start(timer_t_ *t);
void timer_stop(timer_t_ *t);   /* dodaje odcinek do total */

/* Przepustowosc w GB/s (1e9 B); chroni przed dzieleniem przez 0. */
double throughput_gbps(unsigned long long bytes, double seconds);

/* Statystyki przebiegu. Pomiar = 2 wywolania now_sec() wokol fazy i dodanie do pola;
 * NIC nie jest wypisywane w trakcie - raport dopiero po zamknieciu plikow
 * (stats_report), wiec pomiar nie wplywa na szyfrowanie/deszyfrowanie. */
typedef struct {
    double read_s;     /* odczyt pliku */
    double h2d_s;      /* transfer host->device (CUDA) / dystrybucja (MPI) */
    double kernel_s;   /* sama operacja AES-CTR (jadro) */
    double d2h_s;      /* transfer device->host (CUDA) / zbieranie (MPI) */
    double write_s;    /* zapis pliku */
    double total_s;    /* caly potok: od pierwszego odczytu do ostatniego zapisu */
    unsigned long long bytes;
} run_stats;

void stats_reset(run_stats *s);

/* Jedna linia "klucz=wartosc" na stdout: GB/s jadra (bytes/kernel_s) i calego potoku
 * (bytes/total_s) + podzial czasu. variant: "openmp"/"mpi"/"cuda";
 * workers: watki/procesy; buffer: rozmiar bufora/partii w bajtach. */
void stats_report(const run_stats *s, const char *variant, int workers, size_t buffer);

#endif
