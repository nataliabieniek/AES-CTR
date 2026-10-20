#include "timer.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

double now_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}

void timer_reset(timer_t_ *t) { t->start = 0; t->total = 0; }
void timer_start(timer_t_ *t) { t->start = now_sec(); }
void timer_stop(timer_t_ *t) { t->total += now_sec() - t->start; }

void stats_reset(run_stats *s) { memset(s, 0, sizeof *s); }

void stats_report(const run_stats *s, const char *variant, int workers, size_t buffer) {
    printf("variant=%s workers=%d buffer=%zu bytes=%llu read_s=%.6f h2d_s=%.6f kernel_s=%.6f "
           "d2h_s=%.6f write_s=%.6f total_s=%.6f kernel_GBps=%.3f total_GBps=%.3f\n",
           variant, workers, buffer, s->bytes, s->read_s, s->h2d_s, s->kernel_s, s->d2h_s,
           s->write_s, s->total_s, throughput_gbps(s->bytes, s->kernel_s),
           throughput_gbps(s->bytes, s->total_s));
}

double throughput_gbps(unsigned long long bytes, double seconds) {
    return seconds > 0 ? (double)bytes / 1e9 / seconds : 0.0;
}
