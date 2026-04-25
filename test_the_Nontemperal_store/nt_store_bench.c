#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_BYTES (1 * 1024ULL * 1024ULL * 1024ULL)
#define DEFAULT_ITERS 30
#define WARMUP_ITERS 3
#define ALIGNMENT 64

typedef struct __attribute__((aligned(64))) {
    uint64_t words[8];
} Block64;

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static void fill_source(uint8_t *buf, size_t n) {
    uint64_t x = 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < n; i++) {
        x ^= x << 7;
        x ^= x >> 9;
        x ^= x << 8;
        buf[i] = (uint8_t)(x & 0xffU);
    }
}

static void build_gc_dest_index(size_t *idx, size_t blocks) {
    size_t stride = 8191;
    if (blocks == 0) {
        return;
    }
    if (stride >= blocks) {
        stride %= blocks;
    }
    if (stride == 0) {
        stride = 1;
    }

    size_t di = 0;
    for (size_t i = 0; i < blocks; i++) {
        idx[i] = di;
        di += stride;
        if (di >= blocks) {
            di -= blocks;
        }
    }
}

static void normal_c_write_gc(uint8_t *dst,
                              const uint8_t *src,
                              size_t blocks,
                              const size_t *dst_idx) {
    Block64 *d = (Block64 *)dst;
    const Block64 *s = (const Block64 *)src;

    for (size_t i = 0; i < blocks; i++) {
        d[dst_idx[i]] = s[i];
    }
}

static void nt_stream_write_gc(uint8_t *dst,
                               const uint8_t *src,
                               size_t blocks,
                               const size_t *dst_idx) {
#if defined(__x86_64__) || defined(__i386__)

    for (size_t i = 0; i < blocks; i++) {
        const uint8_t *sp = src + i * sizeof(Block64);
        uint8_t *dp = dst + dst_idx[i] * sizeof(Block64);
        __m128i v0 = _mm_load_si128((const __m128i *)(sp + 0));
        __m128i v1 = _mm_load_si128((const __m128i *)(sp + 16));
        __m128i v2 = _mm_load_si128((const __m128i *)(sp + 32));
        __m128i v3 = _mm_load_si128((const __m128i *)(sp + 48));
        _mm_stream_si128((__m128i *)(dp + 0), v0);
        _mm_stream_si128((__m128i *)(dp + 16), v1);
        _mm_stream_si128((__m128i *)(dp + 32), v2);
        _mm_stream_si128((__m128i *)(dp + 48), v3);
        // _mm_sfence();
    }

    _mm_sfence();
#else
    // (void)dst;
    // (void)src;
    // (void)blocks;
    // (void)dst_idx;
#endif
}

static double mean(const double *arr, int n) {
    double s = 0.0;
    for (int i = 0; i < n; i++) s += arr[i];
    return s / (double)n;
}

static double median(double *arr, int n) {
    qsort(arr, (size_t)n, sizeof(double), cmp_double);
    if (n % 2 == 1) return arr[n / 2];
    return 0.5 * (arr[n / 2 - 1] + arr[n / 2]);
}

static void print_stats(const char *name, const double *samples, int n, size_t bytes) {
    double *tmp = (double *)malloc((size_t)n * sizeof(double));
    if (!tmp) {
        fprintf(stderr, "malloc failed for stats buffer\n");
        exit(1);
    }

    memcpy(tmp, samples, (size_t)n * sizeof(double));
    double med = median(tmp, n);
    double avg = mean(samples, n);
    double minv = tmp[0];
    double maxv = tmp[n - 1];

    double gib = (double)bytes / (1024.0 * 1024.0 * 1024.0);
    double bw_med = gib / med;
    double bw_avg = gib / avg;

    printf("[%s]\n", name);
    printf("  median: %.6f s  (%.2f GiB/s)\n", med, bw_med);
    printf("  mean:   %.6f s  (%.2f GiB/s)\n", avg, bw_avg);
    printf("  min:    %.6f s\n", minv);
    printf("  max:    %.6f s\n", maxv);

    free(tmp);
}









int main() {
#if !(defined(__x86_64__) || defined(__i386__))
    fprintf(stderr, "This test currently supports x86/x86_64 only.\n");
    return 1;
#endif


    size_t bytes = DEFAULT_BYTES;
    int iters = DEFAULT_ITERS;

    bytes = (bytes / 64) * 64;
    size_t blocks = bytes / sizeof(Block64);
    printf("num of blocks: %zu\n", blocks);

    uint8_t *src = NULL;
    uint8_t *dst_normal = NULL;
    uint8_t *dst_nt = NULL;
    size_t *dst_idx = NULL;

    if (posix_memalign((void **)&src, ALIGNMENT, bytes) != 0 ||
        posix_memalign((void **)&dst_normal, ALIGNMENT, bytes) != 0 ||
        posix_memalign((void **)&dst_nt, ALIGNMENT, bytes) != 0) {
        fprintf(stderr, "posix_memalign failed: %s\n", strerror(errno));
        free(src);
        free(dst_normal);
        free(dst_nt);
        return 1;
    }

    dst_idx = (size_t *)malloc(blocks * sizeof(size_t));

    build_gc_dest_index(dst_idx, blocks);

    fill_source(src, bytes);
    memset(dst_normal, 0, bytes);
    memset(dst_nt, 0, bytes);

    for (int i = 0; i < WARMUP_ITERS; i++) {
        normal_c_write_gc(dst_normal, src, blocks, dst_idx);
        nt_stream_write_gc(dst_nt, src, blocks, dst_idx);
    }

    double *normal_times = (double *)malloc((size_t)iters * sizeof(double));
    double *nt_times = (double *)malloc((size_t)iters * sizeof(double));


    uint64_t sink = 0;
    for (int i = 0; i < iters; i++) {
        double t0, t1;

        if ((i & 1) == 0) {
            t0 = now_sec();
            normal_c_write_gc(dst_normal, src, blocks, dst_idx);
            t1 = now_sec();
            normal_times[i] = t1 - t0;

            t0 = now_sec();
            nt_stream_write_gc(dst_nt, src, blocks, dst_idx);
            t1 = now_sec();
            nt_times[i] = t1 - t0;
        } else {
            t0 = now_sec();
            nt_stream_write_gc(dst_nt, src, blocks, dst_idx);
            t1 = now_sec();
            nt_times[i] = t1 - t0;

            t0 = now_sec();
            normal_c_write_gc(dst_normal, src, blocks, dst_idx);
            t1 = now_sec();
            normal_times[i] = t1 - t0;
        }

        sink += dst_normal[(size_t)(i * 17) % bytes];
        sink += dst_nt[(size_t)(i * 29) % bytes];
    }

    printf("=== pure C write vs non-temporal write ===\n");
    printf("bytes: %zu (%.2f MiB), iterations: %d, warmup: %d\n",
           bytes, (double)bytes / (1024.0 * 1024.0), iters, WARMUP_ITERS);
    printf("mode: gc-like random store (precomputed destination index)\n\n");

    print_stats("normal C write", normal_times, iters, bytes);
    printf("\n");
    print_stats("NT stream write", nt_times, iters, bytes);

    double normal_med, nt_med;
    {
        double *a = (double *)malloc((size_t)iters * sizeof(double));
        double *b = (double *)malloc((size_t)iters * sizeof(double));
        if (!a || !b) {
            fprintf(stderr, "malloc failed for median comparison\n");
            free(a);
            free(b);
            free(src);
            free(dst_normal);
            free(dst_nt);
            free(dst_idx);
            free(normal_times);
            free(nt_times);
            return 1;
        }
        memcpy(a, normal_times, (size_t)iters * sizeof(double));
        memcpy(b, nt_times, (size_t)iters * sizeof(double));
        normal_med = median(a, iters);
        nt_med = median(b, iters);
        free(a);
        free(b);
    }

    printf("\n[delta by median]\n");
    printf("  NT vs normal: %.2f%% (%s)\n",
           (nt_med - normal_med) / normal_med * 100.0,
           nt_med < normal_med ? "NT faster" : "NT slower");
    printf("  sink(ignore): %" PRIu64 "\n", sink);

    free(src);
    free(dst_normal);
    free(dst_nt);
    free(dst_idx);
    free(normal_times);
    free(nt_times);
    return 0;
}
