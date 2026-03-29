#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__x86_64__) || defined(__i386__)
#include <emmintrin.h>
#endif

#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "cache_dram_manager.h"
#include "gc-visitor-inl.h"

extern long pass_the_remember_set_count;
extern long long generational_forward_count;

namespace {

// LIFO stack that stores young payload pointers for minor traversal.
struct GiYGCStack {
  uintptr_t *items;
  size_t count;
  size_t capacity;
  bool in_cache_space;
};

GiYGCStack g_gc_stack = {NULL, 0, 0, false};

enum EdgePatchKind {
  EDGE_PATCH_JSVALUE_TAGGED,
  EDGE_PATCH_VOID_PTR,
  EDGE_PATCH_FUNC_FRAME,
  EDGE_PATCH_JSVALUE_PTR,
};

struct EdgePatchEntry {
  void *slot_addr;
  EdgePatchKind kind;
};

struct EdgePatchLog {
  EdgePatchEntry *items;
  size_t count;
  size_t capacity;
  bool in_cache_space;
};

EdgePatchLog g_edge_log = {NULL, 0, 0, false};
bool g_used_nt_old_store = false;

static inline bool in_dram_space(uintptr_t ptr) {
	return (ptr - dram_space.begin) < dram_space.total_size;
}

static inline bool in_young_space(uintptr_t ptr) {
	return (ptr - cache_space.work_begin) < (cache_space.end - cache_space.work_begin);
}

static inline bool in_init_space(uintptr_t ptr) {
	return ptr >= cache_space.begin && ptr < cache_space.work_begin;
}

static inline void giy_store_u64_old(uint64_t *slot, uint64_t bits) {
#if defined(__x86_64__)
  _mm_stream_si64((long long *) slot, (long long) bits);
  g_used_nt_old_store = true;
#else
  *slot = bits;
#endif
}

static inline void giy_store_ptr_slot(void **slot, void *value, bool slot_in_dram) {
  if (slot_in_dram) {
    giy_store_u64_old((uint64_t *) slot, (uint64_t) (uintptr_t) value);
    return;
  }
  *slot = value;
}

static inline void giy_store_jsvalue_slot(JSValue *slot, JSValue value, bool slot_in_dram) {
  if (slot_in_dram) {
    giy_store_u64_old((uint64_t *) slot, (uint64_t) (uintjsv_t) value);
    return;
  }
  *slot = value;
}

static void giy_bind_stack_to_cache_impl() {
  if (g_gc_stack.in_cache_space && g_edge_log.in_cache_space)
    return;

  if (cache_space.work_begin == 0 || cache_space.end <= cache_space.work_begin) {
    printf("GiY stack bind failed: invalid cache work area\n");
    exit(1);
  }

  // Reserve a small fixed fraction of young area for GC auxiliaries.
  size_t young_bytes = (size_t) (cache_space.end - cache_space.work_begin);
  size_t stack_bytes = (young_bytes / 8) & ~(sizeof(uintptr_t) - 1);
  size_t edge_bytes = (young_bytes / 8) & ~(sizeof(uintptr_t) - 1);
  if (stack_bytes < 1024 * sizeof(uintptr_t))
    stack_bytes = 1024 * sizeof(uintptr_t);
  if (edge_bytes < 2048 * sizeof(EdgePatchEntry))
    edge_bytes = 2048 * sizeof(EdgePatchEntry);

  size_t reserve_total = stack_bytes + edge_bytes;
  if (reserve_total >= young_bytes) {
    printf("GiY aux bind failed: not enough cache bytes (%zu)\n", young_bytes);
    exit(1);
  }

  uintptr_t edge_begin = cache_space.end - edge_bytes;
  uintptr_t stack_begin = edge_begin - stack_bytes;
  cache_space.end = stack_begin;
  cache_space.total_size -= (int) reserve_total;

  g_gc_stack.items = (uintptr_t *) stack_begin;
  g_gc_stack.count = 0;
  g_gc_stack.capacity = stack_bytes / sizeof(uintptr_t);
  g_gc_stack.in_cache_space = true;

  g_edge_log.items = (EdgePatchEntry *) edge_begin;
  g_edge_log.count = 0;
  g_edge_log.capacity = edge_bytes / sizeof(EdgePatchEntry);
  g_edge_log.in_cache_space = true;
}

static void ensure_gc_stack_capacity() {
  if (g_gc_stack.items != NULL)
    return;

  printf("GiY stack not bound to cache space before minor GC\n");
  exit(1);
}

static inline void gc_stack_reset() {
  g_gc_stack.count = 0;
}

static inline void gc_stack_push(uintptr_t payload_ptr) {
  if (g_gc_stack.count >= g_gc_stack.capacity) {
    printf("GiY stack overflow in cache space (capacity=%zu)\n", g_gc_stack.capacity);
    exit(1);
  }
  g_gc_stack.items[g_gc_stack.count++] = payload_ptr;
}

static inline bool gc_stack_empty() {
  return g_gc_stack.count == 0;
}

static inline uintptr_t gc_stack_pop() {
  return g_gc_stack.items[--g_gc_stack.count];
}

static void ensure_edge_log_capacity() {
  if (g_edge_log.items != NULL)
    return;

  printf("GiY edge log not bound to cache space before minor GC\n");
  exit(1);
}

static inline void edge_log_reset() {
  g_edge_log.count = 0;
}

static inline void edge_log_push(void *slot_addr, EdgePatchKind kind) {
  if (g_edge_log.count >= g_edge_log.capacity) {
    printf("GiY edge log overflow in cache space (capacity=%zu)\n", g_edge_log.capacity);
    exit(1);
  }
  g_edge_log.items[g_edge_log.count].slot_addr = slot_addr;
  g_edge_log.items[g_edge_log.count].kind = kind;
  g_edge_log.count++;
}

// Reserve destination in old generation and mark source young object.
static uintptr_t copy_for_minor(uintptr_t payload_ptr) {
  object_header *hdr = (object_header *) payload_ptr - 1;

  if (hdr->forwarding_pointer != 0)
    return hdr->forwarding_pointer;

  int align_bytes = ALIGN(hdr->size + sizeof(object_header));
  if (dram_space.available_bytes < (size_t) align_bytes) {
    printf("DRAM space full in copy_for_minor (%d bytes)\n", align_bytes);
    exit(1);
  }

  object_header *dest_hdr = (object_header *) dram_space.free;
  dram_space.free += align_bytes;
  dram_space.available_bytes -= align_bytes;

  hdr->forwarding_pointer = (uintptr_t) (dest_hdr + 1);
  gc_stack_push(payload_ptr);
  return hdr->forwarding_pointer;
}

// Reserve-only tracer: discovers young edges and allocates destination slots,
// but does not rewrite the currently scanned object fields in-place.
class GiYReserveTracer {
private:
  static uintptr_t forward(uintptr_t ptr) {
    generational_forward_count++;

    if (in_dram_space(ptr))
      return ptr;
    if (in_init_space(ptr))
      return ptr;
    if (!in_young_space(ptr))
      return ptr;

    return copy_for_minor(ptr);
  }

public:
  static constexpr bool is_single_object_scanner = false;
  static constexpr bool is_hcg_mutator = false;

  static void process_edge(JSValue &v) {
    if (is_fixnum(v) || is_special(v))
      return;

    uintptr_t ptr = (uintptr_t) clear_ptag(v);
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    edge_log_push((void *) &v, EDGE_PATCH_JSVALUE_TAGGED);
    (void) forward(ptr);
  }

  static void process_edge(void *&p) {
    uintptr_t ptr = (uintptr_t) p;
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    edge_log_push((void *) &p, EDGE_PATCH_VOID_PTR);
    (void) forward(ptr);
  }

  static void process_edge_function_frame(JSValue &v) {
    void *p = jsv_to_function_frame(v);
    uintptr_t ptr = (uintptr_t) p;
    if (ptr == 0 || !in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    edge_log_push((void *) &v, EDGE_PATCH_FUNC_FRAME);
    (void) forward(ptr);
  }

  static void process_edge_ex_JSValue_array(JSValue *&array, size_t size) {
    (void) size;
    if (array == NULL)
      return;

    uintptr_t ptr = (uintptr_t) array;
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    edge_log_push((void *) &array, EDGE_PATCH_JSVALUE_PTR);
    (void) forward(ptr);
  }

  static void process_edge_ex_JSValue_array(JSValue &array_ref, size_t size) {
    (void) size;
    JSValue *array = (JSValue *) clear_ptag(array_ref);
    if (array == NULL)
      return;

    uintptr_t ptr = (uintptr_t) array;
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    edge_log_push((void *) &array_ref, EDGE_PATCH_JSVALUE_TAGGED);
    (void) forward(ptr);
  }

  static void process_node_JSValue_array(JSValue *p) {
    object_header *hdr = ((object_header *) p) - 1;
    size_t slots = (size_t) hdr->size / sizeof(JSValue);
    for (size_t i = 0; i < slots; i++)
      process_edge(p[i]);
  }

  static void process_mark_stack() {}

  static void process_edge_ex_ptr_array(void **&array, size_t size) {
    (void) size;
    if (array == NULL)
      return;

    uintptr_t ptr = (uintptr_t) array;
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    edge_log_push((void *) &array, EDGE_PATCH_VOID_PTR);
    (void) forward(ptr);
  }

  static void process_weak_edge(JSValue &v) { process_edge(v); }
  static void process_weak_edge(void *&p) { process_edge(p); }

  static bool is_marked_cell(void *p) {
    if (p == NULL)
      return false;
    uintptr_t ptr = (uintptr_t) p;
    if (in_dram_space(ptr) || in_init_space(ptr))
      return true;
    if (in_young_space(ptr)) {
      object_header *hdr = ((object_header *) ptr) - 1;
      return hdr->forwarding_pointer != 0;
    }
    return false;
  }
};

class GiYPatchTracer {
private:
  static uintptr_t patch_ptr(uintptr_t ptr) {
    if (in_dram_space(ptr) || in_init_space(ptr) || !in_young_space(ptr))
      return ptr;

    object_header *hdr = (object_header *) ptr - 1;
    if (hdr->forwarding_pointer == 0)
      return ptr;
    return hdr->forwarding_pointer;
  }

public:
  static constexpr bool is_single_object_scanner = false;
  static constexpr bool is_hcg_mutator = false;

  static void process_edge(JSValue &v) {
    if (is_fixnum(v) || is_special(v))
      return;
    uintptr_t ptr = (uintptr_t) clear_ptag(v);
    uintptr_t to = patch_ptr(ptr);
    if (to != ptr) {
      JSValue nv = put_ptag(to, get_ptag(v));
      bool slot_in_dram = in_dram_space((uintptr_t) &v);
      giy_store_jsvalue_slot(&v, nv, slot_in_dram);
    }
  }

  static void process_edge(void *&p) {
    uintptr_t ptr = (uintptr_t) p;
    uintptr_t to = patch_ptr(ptr);
    if (to != ptr) {
      bool slot_in_dram = in_dram_space((uintptr_t) &p);
      giy_store_ptr_slot(&p, (void *) to, slot_in_dram);
    }
  }

  static void process_edge_function_frame(JSValue &v) {
    void *p = jsv_to_function_frame(v);
    uintptr_t ptr = (uintptr_t) p;
    uintptr_t to = patch_ptr(ptr);
    if (to != ptr) {
      JSValue nv = (JSValue) (uintjsv_t) to;
      bool slot_in_dram = in_dram_space((uintptr_t) &v);
      giy_store_jsvalue_slot(&v, nv, slot_in_dram);
    }
  }

  static void process_edge_ex_JSValue_array(JSValue *&array, size_t size) {
    (void) size;
    if (array == NULL)
      return;
    uintptr_t ptr = (uintptr_t) array;
    uintptr_t to = patch_ptr(ptr);
    if (to != ptr) {
      bool slot_in_dram = in_dram_space((uintptr_t) &array);
      giy_store_ptr_slot((void **) &array, (void *) to, slot_in_dram);
    }
  }

  static void process_edge_ex_JSValue_array(JSValue &array_ref, size_t size) {
    (void) size;
    JSValue *array = (JSValue *) clear_ptag(array_ref);
    if (array == NULL)
      return;
    uintptr_t ptr = (uintptr_t) array;
    uintptr_t to = patch_ptr(ptr);
    if (to != ptr) {
      JSValue nv = put_ptag(to, get_ptag(array_ref));
      bool slot_in_dram = in_dram_space((uintptr_t) &array_ref);
      giy_store_jsvalue_slot(&array_ref, nv, slot_in_dram);
    }
  }

  static void process_node_JSValue_array(JSValue *p) {
    object_header *hdr = ((object_header *) p) - 1;
    size_t slots = (size_t) hdr->size / sizeof(JSValue);
    for (size_t i = 0; i < slots; i++)
      process_edge(p[i]);
  }

  static void process_mark_stack() {}

  static void process_edge_ex_ptr_array(void **&array, size_t size) {
    (void) size;
    if (array == NULL)
      return;
    uintptr_t ptr = (uintptr_t) array;
    uintptr_t to = patch_ptr(ptr);
    if (to != ptr) {
      bool slot_in_dram = in_dram_space((uintptr_t) &array);
      giy_store_ptr_slot((void **) &array, (void *) to, slot_in_dram);
    }
  }

  static void process_weak_edge(JSValue &v) { process_edge(v); }
  static void process_weak_edge(void *&p) { process_edge(p); }

  static bool is_marked_cell(void *p) {
    if (p == NULL)
      return false;
    uintptr_t ptr = (uintptr_t) p;
    if (in_dram_space(ptr) || in_init_space(ptr))
      return true;
    if (in_young_space(ptr)) {
      object_header *hdr = ((object_header *) ptr) - 1;
      return hdr->forwarding_pointer != 0;
    }
    return false;
  }
};

static void giy_scan_remembered_set_slots() {
	for (int i = 0; i < remembered_set.count; i++) {
		uintptr_t raw_slot = remembered_set.buffer[i];
		bool is_ptr_slot = (raw_slot & 1) != 0;
		uintptr_t slot_addr = raw_slot & ~(uintptr_t) 1;

		bool slot_in_dram = (slot_addr >= dram_space.begin && slot_addr < dram_space.end);
		bool slot_in_init = (slot_addr >= cache_space.begin && slot_addr < cache_space.work_begin);
		if (!slot_in_dram && !slot_in_init)
			continue;

    if (is_ptr_slot) {
      void **slot = (void **) slot_addr;
      void *value = *slot;
      GiYReserveTracer::process_edge(value);
      giy_store_ptr_slot(slot, value, slot_in_dram);
    } else {
      JSValue *slot = (JSValue *) slot_addr;
      JSValue value = *slot;
      GiYReserveTracer::process_edge(value);
      giy_store_jsvalue_slot(slot, value, slot_in_dram);
    }
	}
}

static void giy_patch_remembered_set_slots() {
  for (int i = 0; i < remembered_set.count; i++) {
    uintptr_t raw_slot = remembered_set.buffer[i];
    bool is_ptr_slot = (raw_slot & 1) != 0;
    uintptr_t slot_addr = raw_slot & ~(uintptr_t) 1;

    bool slot_in_dram = (slot_addr >= dram_space.begin && slot_addr < dram_space.end);
    bool slot_in_init = (slot_addr >= cache_space.begin && slot_addr < cache_space.work_begin);
    if (!slot_in_dram && !slot_in_init)
      continue;

    if (is_ptr_slot) {
      void **slot = (void **) slot_addr;
      void *value = *slot;
      GiYPatchTracer::process_edge(value);
      giy_store_ptr_slot(slot, value, slot_in_dram);
    } else {
      JSValue *slot = (JSValue *) slot_addr;
      JSValue value = *slot;
      GiYPatchTracer::process_edge(value);
      giy_store_jsvalue_slot(slot, value, slot_in_dram);
    }
  }
}

static inline uintptr_t forwarded_or_self(uintptr_t ptr) {
  if (!in_young_space(ptr))
    return ptr;

  object_header *hdr = (object_header *) ptr - 1;
  if (hdr->forwarding_pointer == 0)
    return ptr;
  return hdr->forwarding_pointer;
}

static void giy_apply_edge_log() {
  for (size_t i = 0; i < g_edge_log.count; i++) {
    EdgePatchEntry &e = g_edge_log.items[i];

    switch (e.kind) {
    case EDGE_PATCH_JSVALUE_TAGGED: {
      JSValue *slot = (JSValue *) e.slot_addr;
      JSValue oldv = *slot;
      if (is_fixnum(oldv) || is_special(oldv))
        break;
      uintptr_t from = (uintptr_t) clear_ptag(oldv);
      uintptr_t to = forwarded_or_self(from);
      if (to != from)
        *slot = put_ptag(to, get_ptag(oldv));
      break;
    }
    case EDGE_PATCH_VOID_PTR: {
      void **slot = (void **) e.slot_addr;
      uintptr_t from = (uintptr_t) (*slot);
      uintptr_t to = forwarded_or_self(from);
      if (to != from)
        *slot = (void *) to;
      break;
    }
    case EDGE_PATCH_FUNC_FRAME: {
      JSValue *slot = (JSValue *) e.slot_addr;
      uintptr_t from = (uintptr_t) jsv_to_function_frame(*slot);
      uintptr_t to = forwarded_or_self(from);
      if (to != from)
        *slot = (JSValue) (uintjsv_t) to;
      break;
    }
    case EDGE_PATCH_JSVALUE_PTR: {
      JSValue **slot = (JSValue **) e.slot_addr;
      uintptr_t from = (uintptr_t) (*slot);
      uintptr_t to = forwarded_or_self(from);
      if (to != from)
        *slot = (JSValue *) to;
      break;
    }
    }
  }
}

static inline void giy_copy_live_object(void *dst,
                                        const void *src,
                                        size_t nbytes,
                                        bool *used_nt_store) {
#if defined(__x86_64__) || defined(__i386__)
  unsigned char *d = (unsigned char *) dst;
  const unsigned char *s = (const unsigned char *) src;
  size_t n = nbytes;

  if ((((uintptr_t) d) & 15) != 0) {
    if ((((uintptr_t) d) & 15) != 8 || n < 8) {
      printf("GiY NT copy alignment invariant failed (dst=%p, n=%zu)\n", (void *) d, n);
      exit(1);
    }
    uint64_t v;
    memcpy(&v, s, sizeof(v));
    _mm_stream_si64((long long *) d, (long long) v);
    s += 8;
    d += 8;
    n -= 8;
  }

  while (n >= 16) {
    __m128i v = _mm_loadu_si128((const __m128i *) s);
    _mm_stream_si128((__m128i *) d, v);
    s += 16;
    d += 16;
    n -= 16;
  }

  if (n != 0) {
    if (n != 8) {
      printf("GiY NT copy tail invariant failed (n=%zu)\n", n);
      exit(1);
    }
    uint64_t v;
    memcpy(&v, s, sizeof(v));
    _mm_stream_si64((long long *) d, (long long) v);
  }

  *used_nt_store = true;
  return;
#else
  (void) used_nt_store;
#endif

  memcpy(dst, src, nbytes);
}

static void giy_traverse_stack_and_copy() {
	bool used_nt_store = false;
	while (!gc_stack_empty()) {
		uintptr_t src_payload = gc_stack_pop();
		object_header *src_hdr = (object_header *) src_payload - 1;

    // Traverse and reserve children, while recording in-object edge slots.
    edge_log_reset();
    process_node<GiYReserveTracer>(src_hdr->type, src_payload);
    // Patch this young object's fields to destination pointers before memcpy.
    giy_apply_edge_log();

		uintptr_t dst_payload = src_hdr->forwarding_pointer;
		if (dst_payload == 0) {
			printf("GiY invariant violated: missing forwarding pointer\n");
			exit(1);
		}

		object_header *dst_hdr = ((object_header *) dst_payload) - 1;
		int align_bytes = ALIGN(src_hdr->size + sizeof(object_header));

    // Materialize in old generation with non-temporal stores when beneficial.
    giy_copy_live_object((void *) dst_hdr,
                         (const void *) src_hdr,
                         (size_t) align_bytes,
                         &used_nt_store);
	}

  if (used_nt_store)
    g_used_nt_old_store = true;
}

}  // namespace

void giy_bind_stack_to_cache() {
  giy_bind_stack_to_cache_impl();
}

void giy_minor_collect(Context *ctx,
											 long long *scan_roots_ns,
											 long long *scan_rs_ns,
											 long long *young_trace_ns) {
	ensure_gc_stack_capacity();
  ensure_edge_log_capacity();
	gc_stack_reset();
  g_used_nt_old_store = false;

	struct timespec t1, t2, t3, t4;
	clock_gettime(CLOCK_MONOTONIC, &t1);
  scan_roots<GiYReserveTracer>(ctx);
	clock_gettime(CLOCK_MONOTONIC, &t2);

#ifdef USE_REMEMBERED_SET
	giy_scan_remembered_set_slots();
#endif
	clock_gettime(CLOCK_MONOTONIC, &t3);

	giy_traverse_stack_and_copy();
  scan_roots<GiYPatchTracer>(ctx);
#ifdef USE_REMEMBERED_SET
  giy_patch_remembered_set_slots();
#endif
#if defined(__x86_64__) || defined(__i386__)
  if (g_used_nt_old_store)
    _mm_sfence();
#endif
	clock_gettime(CLOCK_MONOTONIC, &t4);

	if (scan_roots_ns != NULL)
		*scan_roots_ns += (t2.tv_sec - t1.tv_sec) * 1000000000LL + (t2.tv_nsec - t1.tv_nsec);
	if (scan_rs_ns != NULL)
		*scan_rs_ns += (t3.tv_sec - t2.tv_sec) * 1000000000LL + (t3.tv_nsec - t2.tv_nsec);
	if (young_trace_ns != NULL)
		*young_trace_ns += (t4.tv_sec - t3.tv_sec) * 1000000000LL + (t4.tv_nsec - t3.tv_nsec);
}

void giy_weak_clear(Context *ctx) {
  weak_clear<GiYPatchTracer>(ctx);
}
