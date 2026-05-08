#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <immintrin.h>
#include <sched.h>
#include <unistd.h>

static constexpr size_t kYoungBytes = 512 * 1024;
static constexpr size_t kOldBytes = 256 * 1024 * 1024;
static constexpr size_t kAlign = 64;

static double now_sec() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

static void pin_cpu0() {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(0, &set);
  sched_setaffinity(0, sizeof(set), &set);
}

static void *alloc_aligned(size_t bytes) {
  void *p = nullptr;
  if (posix_memalign(&p, kAlign, bytes) != 0) {
    perror("posix_memalign");
    exit(1);
  }
  memset(p, 0xA5, bytes);
  return p;
}

static inline void nt_copy_128(void *dst, const void *src, size_t nbytes) {
  unsigned char *d = (unsigned char *) dst;
  const unsigned char *s = (const unsigned char *) src;
  size_t n = nbytes;
  while (n >= 16) {
    __m128i v = _mm_loadu_si128((const __m128i *) s);
    _mm_stream_si128((__m128i *) d, v);
    s += 16;
    d += 16;
    n -= 16;
  }
  if (n != 0)
    memcpy(d, s, n);
}

static inline void nt_copy_256(void *dst, const void *src, size_t nbytes) {
#if defined(__AVX2__)
  unsigned char *d = (unsigned char *) dst;
  const unsigned char *s = (const unsigned char *) src;
  size_t n = nbytes;
  while (n >= 32) {
    __m256i v = _mm256_loadu_si256((const __m256i *) s);
    _mm256_stream_si256((__m256i *) d, v);
    s += 32;
    d += 32;
    n -= 32;
  }
  if (n != 0)
    memcpy(d, s, n);
#else
  nt_copy_128(dst, src, nbytes);
#endif
}

static uint64_t probe_hot_young(unsigned char *young, size_t bytes, int rounds) {
  volatile uint64_t sum = 0;
  for (int r = 0; r < rounds; r++) {
    for (size_t i = 0; i < bytes; i += 64)
      sum += young[i];
  }
  return sum;
}

struct Result {
  double sec;
  uint64_t checksum;
};

template <typename Fn>
static Result run_copy(Fn fn,
                       unsigned char *src_base,
                       size_t src_span,
                       unsigned char *dst_base,
                       size_t dst_span,
                       size_t nbytes,
                       size_t total_bytes,
                       bool sfence_after_each) {
  size_t iters = std::max((size_t) 1, total_bytes / nbytes);
  size_t src_mask = src_span - 1;
  size_t dst_mask = dst_span - 1;
  volatile uint64_t checksum = 0;

  double t0 = now_sec();
  for (size_t i = 0; i < iters; i++) {
    size_t src_off = (i * 131) & src_mask;
    size_t dst_off = (i * nbytes) & dst_mask;
    src_off &= ~(size_t) 63;
    dst_off &= ~(size_t) 63;
    fn(dst_base + dst_off, src_base + src_off, nbytes);
    if (sfence_after_each)
      _mm_sfence();
    checksum += dst_base[dst_off];
  }
  if (!sfence_after_each)
    _mm_sfence();
  double t1 = now_sec();
  return {t1 - t0, checksum};
}

static void run_one_size(size_t nbytes, unsigned char *young, unsigned char *old) {
  size_t total_bytes = 1024ull * 1024ull * 1024ull;
  if (nbytes <= 128)
    total_bytes = 512ull * 1024ull * 1024ull;

  unsigned char *young_src = young;
  unsigned char *young_dst = young + kYoungBytes;

  Result yy = run_copy(
    [](void *d, const void *s, size_t n) { memcpy(d, s, n); },
    young_src, kYoungBytes, young_dst, kYoungBytes, nbytes, total_bytes, false);

  Result yo = run_copy(
    [](void *d, const void *s, size_t n) { memcpy(d, s, n); },
    young_src, kYoungBytes, old, kOldBytes, nbytes, total_bytes, false);

  Result nt128 = run_copy(nt_copy_128, young_src, kYoungBytes, old, kOldBytes,
                          nbytes, total_bytes, false);

  Result nt256 = run_copy(nt_copy_256, young_src, kYoungBytes, old, kOldBytes,
                          nbytes, total_bytes, false);

  size_t fenced_total =
    std::min(total_bytes, (size_t) (128ull * 1024ull * 1024ull));
  Result nt128_fenced =
    run_copy(nt_copy_128, young_src, kYoungBytes, old, kOldBytes,
             nbytes, fenced_total, true);

  double copies = (double) std::max((size_t) 1, total_bytes / nbytes);
  auto gbps = [](size_t bytes, double sec) {
    return ((double) bytes / sec) / (1024.0 * 1024.0 * 1024.0);
  };

  printf("%5zu,%9.3f,%9.3f,%9.3f,%9.3f,%9.3f,%9.2f,%9.2f,%9.2f,%9.2f,%9.2f,%llu\n",
         nbytes,
         gbps(total_bytes, yy.sec),
         gbps(total_bytes, yo.sec),
         gbps(total_bytes, nt128.sec),
         gbps(total_bytes, nt256.sec),
         gbps(fenced_total, nt128_fenced.sec),
         yy.sec * 1e9 / copies,
         yo.sec * 1e9 / copies,
         nt128.sec * 1e9 / copies,
         nt256.sec * 1e9 / copies,
         nt128_fenced.sec * 1e9 /
           (double) std::max((size_t) 1, fenced_total / nbytes),
         (unsigned long long) (yy.checksum + yo.checksum + nt128.checksum +
                               nt256.checksum + nt128_fenced.checksum));
}

static void pollution_probe(size_t nbytes,
                            unsigned char *young,
                            unsigned char *old) {
  size_t total_bytes = 512ull * 1024ull * 1024ull;
  unsigned char *young_src = young;
  unsigned char *young_dst = young + kYoungBytes;

  probe_hot_young(young, kYoungBytes, 16);
  double t0 = now_sec();
  uint64_t base_sum = probe_hot_young(young, kYoungBytes, 256);
  double base = now_sec() - t0;

  run_copy([](void *d, const void *s, size_t n) { memcpy(d, s, n); },
           young_src, kYoungBytes, young_dst, kYoungBytes, nbytes, total_bytes,
           false);
  t0 = now_sec();
  uint64_t yy_sum = probe_hot_young(young, kYoungBytes, 256);
  double after_yy = now_sec() - t0;

  run_copy([](void *d, const void *s, size_t n) { memcpy(d, s, n); },
           young_src, kYoungBytes, old, kOldBytes, nbytes, total_bytes, false);
  t0 = now_sec();
  uint64_t yo_sum = probe_hot_young(young, kYoungBytes, 256);
  double after_yo = now_sec() - t0;

  run_copy(nt_copy_128, young_src, kYoungBytes, old, kOldBytes, nbytes,
           total_bytes, false);
  t0 = now_sec();
  uint64_t nt_sum = probe_hot_young(young, kYoungBytes, 256);
  double after_nt = now_sec() - t0;

  printf("pollution,%zu,base_ms=%.3f,after_young_memcpy_ms=%.3f,after_old_memcpy_ms=%.3f,after_nt_ms=%.3f,checksum=%llu\n",
         nbytes, base * 1e3, after_yy * 1e3, after_yo * 1e3, after_nt * 1e3,
         (unsigned long long) (base_sum + yy_sum + yo_sum + nt_sum));
}

int main() {
  pin_cpu0();
  unsigned char *young =
    (unsigned char *) alloc_aligned(kYoungBytes * 2 + kAlign);
  unsigned char *old = (unsigned char *) alloc_aligned(kOldBytes + kAlign);

  for (size_t i = 0; i < kYoungBytes * 2; i++)
    young[i] = (unsigned char) (i * 131u + 7u);
  for (size_t i = 0; i < kOldBytes; i += 4096)
    old[i] = (unsigned char) i;

  printf("size,young_memcpy_GBps,young_to_old_memcpy_GBps,nt128_GBps,nt256_GBps,nt128_sfence_each_GBps,young_memcpy_ns,young_to_old_memcpy_ns,nt128_ns,nt256_ns,nt128_sfence_each_ns,checksum\n");
  size_t sizes[] = {16, 32, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096, 8192,
                    16384};
  for (size_t n : sizes)
    run_one_size(n, young, old);

  pollution_probe(64, young, old);
  pollution_probe(256, young, old);
  pollution_probe(4096, young, old);

  free(young);
  free(old);
  return 0;
}
