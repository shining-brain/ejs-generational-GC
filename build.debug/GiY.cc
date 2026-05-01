#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/mman.h>
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

struct GiYFTSlotSet {
  uintptr_t *items;
  size_t count;
  size_t capacity;
  bool in_cache_space;
};

GiYFTSlotSet g_ft_slot_set = {NULL, 0, 0, false};
bool g_used_nt_old_store = false;

static const uintptr_t GIY_FT_PTR_SLOT_TAG = 1;
static const unsigned int GIY_PROFILE_CELL_TYPES = 256;
static const unsigned int GIY_PROFILE_EDGE_KINDS = 4;
static const size_t GIY_NT_COPY_MIN_BYTES = 256;

#ifndef GIY_PROFILE_DETAIL
#define GIY_PROFILE_DETAIL 0
#endif

struct GiYProfile {
  unsigned long long minor_collections;
  unsigned long long stack_pushes;
  unsigned long long max_stack_depth;

  unsigned long long edge_entries;
  unsigned long long edge_entries_by_kind[GIY_PROFILE_EDGE_KINDS];
  unsigned long long edge_entries_by_type[GIY_PROFILE_CELL_TYPES];
  unsigned long long edge_patched;
  unsigned long long edge_noop;
  unsigned long long edge_immediate;
  unsigned long long edge_unforwarded_young;
  unsigned long long max_edge_log_per_object;
  unsigned long long max_edge_log_per_gc;

  unsigned long long materialized_objects;
  unsigned long long materialized_bytes;
  unsigned long long materialized_le_64;
  unsigned long long materialized_le_128;
  unsigned long long materialized_le_256;
  unsigned long long materialized_gt_256;
  unsigned long long materialized_objects_by_type[GIY_PROFILE_CELL_TYPES];
  unsigned long long materialized_bytes_by_type[GIY_PROFILE_CELL_TYPES];
  unsigned long long max_materialized_bytes_per_gc;
  unsigned long long current_materialized_bytes_per_gc;

  unsigned long long nt_copy_objects;
  unsigned long long nt_copy_bytes;
  unsigned long long old_slot_stream_stores;

  unsigned long long ft_slots_recorded;
  unsigned long long max_ft_slot_set;
  unsigned long long ft_slots_scanned;
  unsigned long long ft_slots_patched;

  unsigned long long rset_slots_scanned;
  unsigned long long rset_slots_patched;

  size_t aux_stack_bytes;
  size_t aux_edge_bytes;
  size_t aux_ft_slot_bytes;
  size_t aux_total_bytes;
  size_t young_bytes_before_aux;
  size_t young_bytes_after_aux;
};

GiYProfile giy_profile = {};
unsigned int giy_profile_current_cell_type = GIY_PROFILE_CELL_TYPES;

bool g_old_guard_checked = false;
bool g_old_guard_enabled = false;
bool g_old_guard_active = false;
bool g_old_guard_handler_installed = false;
long g_old_guard_page_size = 0;
const char *g_old_guard_phase = "inactive";

static inline bool in_dram_space(uintptr_t ptr) {
	return (ptr - dram_space.begin) < dram_space.total_size;
}

static inline bool in_young_space(uintptr_t ptr) {
	return (ptr - cache_space.work_begin) < (cache_space.end - cache_space.work_begin);
}

static inline bool in_init_space(uintptr_t ptr) {
	return ptr >= cache_space.begin && ptr < cache_space.work_begin;
}

static const char *giy_profile_cell_type_name(unsigned int type) {
  switch ((cell_type_t) type) {
  case CELLT_FREE:
    return "FREE";
  case CELLT_STRING:
    return "STRING";
  case CELLT_FLONUM:
    return "FLONUM";
  case CELLT_SIMPLE_OBJECT:
    return "SIMPLE_OBJECT";
  case CELLT_ARRAY:
    return "ARRAY";
  case CELLT_FUNCTION:
    return "FUNCTION";
  case CELLT_BUILTIN:
    return "BUILTIN";
  case CELLT_ITERATOR:
    return "ITERATOR";
  case CELLT_REGEXP:
    return "REGEXP";
  case CELLT_BOXED_STRING:
    return "BOXED_STRING";
  case CELLT_BOXED_NUMBER:
    return "BOXED_NUMBER";
  case CELLT_BOXED_BOOLEAN:
    return "BOXED_BOOLEAN";
  case CELLT_PROP:
    return "PROP";
  case CELLT_ARRAY_DATA:
    return "ARRAY_DATA";
  case CELLT_BYTE_ARRAY:
    return "BYTE_ARRAY";
  case CELLT_FUNCTION_FRAME:
    return "FUNCTION_FRAME";
  case CELLT_STR_CONS:
    return "STR_CONS";
  case CELLT_TRANSITIONS:
    return "TRANSITIONS";
  case CELLT_HASHTABLE:
    return "HASHTABLE";
  case CELLT_PROPERTY_MAP:
    return "PROPERTY_MAP";
  case CELLT_SHAPE:
    return "SHAPE";
  case CELLT_UNWIND:
    return "UNWIND";
  case CELLT_PROPERTY_MAP_LIST:
    return "PROPERTY_MAP_LIST";
  default:
    return "UNKNOWN";
  }
}

static const char *giy_profile_edge_kind_name(unsigned int kind) {
  switch ((EdgePatchKind) kind) {
  case EDGE_PATCH_JSVALUE_TAGGED:
    return "JSVALUE_TAGGED";
  case EDGE_PATCH_VOID_PTR:
    return "VOID_PTR";
  case EDGE_PATCH_FUNC_FRAME:
    return "FUNC_FRAME";
  case EDGE_PATCH_JSVALUE_PTR:
    return "JSVALUE_PTR";
  default:
    return "UNKNOWN";
  }
}

static inline void giy_profile_note_stack_depth() {
  if (!GIY_PROFILE_DETAIL)
    return;
  if (g_gc_stack.count > giy_profile.max_stack_depth)
    giy_profile.max_stack_depth = g_gc_stack.count;
}

static inline void giy_profile_begin_minor_gc() {
  if (!GIY_PROFILE_DETAIL)
    return;
  giy_profile.minor_collections++;
  giy_profile.current_materialized_bytes_per_gc = 0;
}

static inline void giy_profile_note_current_gc_edge_log() {
  if (!GIY_PROFILE_DETAIL)
    return;
  if (g_edge_log.count > giy_profile.max_edge_log_per_gc)
    giy_profile.max_edge_log_per_gc = g_edge_log.count;
}

static inline void giy_profile_note_young_edge(EdgePatchKind kind) {
  if (!GIY_PROFILE_DETAIL)
    return;
  g_edge_log.count++;
  giy_profile.edge_entries++;
  if ((unsigned int) kind < GIY_PROFILE_EDGE_KINDS)
    giy_profile.edge_entries_by_kind[(unsigned int) kind]++;
  if (giy_profile_current_cell_type < GIY_PROFILE_CELL_TYPES)
    giy_profile.edge_entries_by_type[giy_profile_current_cell_type]++;
  if (g_edge_log.count > giy_profile.max_edge_log_per_object)
    giy_profile.max_edge_log_per_object = g_edge_log.count;
  giy_profile_note_current_gc_edge_log();
}

static inline void giy_profile_note_materialized(cell_type_t type, size_t bytes) {
  if (!GIY_PROFILE_DETAIL)
    return;
  unsigned int t = (unsigned int) type;
  giy_profile.materialized_objects++;
  giy_profile.materialized_bytes += bytes;
  giy_profile.current_materialized_bytes_per_gc += bytes;
  if (giy_profile.current_materialized_bytes_per_gc >
      giy_profile.max_materialized_bytes_per_gc) {
    giy_profile.max_materialized_bytes_per_gc =
      giy_profile.current_materialized_bytes_per_gc;
  }

  if (bytes <= 64)
    giy_profile.materialized_le_64++;
  else if (bytes <= 128)
    giy_profile.materialized_le_128++;
  else if (bytes <= 256)
    giy_profile.materialized_le_256++;
  else
    giy_profile.materialized_gt_256++;

  if (t < GIY_PROFILE_CELL_TYPES) {
    giy_profile.materialized_objects_by_type[t]++;
    giy_profile.materialized_bytes_by_type[t] += bytes;
  }
}

static bool giy_old_guard_enabled() {
  if (!g_old_guard_checked) {
    const char *env = getenv("GIY_MPROTECT_OLD");
    g_old_guard_enabled = (env != NULL && env[0] != '\0' && strcmp(env, "0") != 0);
    g_old_guard_checked = true;
  }
  return g_old_guard_enabled;
}

static uintptr_t giy_page_down(uintptr_t ptr) {
  uintptr_t page = (uintptr_t) g_old_guard_page_size;
  return ptr & ~(page - 1);
}

static uintptr_t giy_page_up(uintptr_t ptr) {
  uintptr_t page = (uintptr_t) g_old_guard_page_size;
  return (ptr + page - 1) & ~(page - 1);
}

static void giy_old_guard_set_range(void *addr, size_t len, int prot) {
  if (len == 0)
    return;
  uintptr_t begin = giy_page_down((uintptr_t) addr);
  uintptr_t end = giy_page_up((uintptr_t) addr + len);
  if (mprotect((void *) begin, end - begin, prot) != 0) {
    printf("GiY mprotect failed addr=%p len=%zu prot=%d errno=%d\n",
           (void *) begin, (size_t) (end - begin), prot, errno);
    exit(1);
  }
}

static void giy_old_guard_sigsegv(int sig, siginfo_t *info, void *uctx) {
  (void) sig;
  (void) uctx;
  if (!g_old_guard_active) {
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
    _exit(128 + SIGSEGV);
  }

  char buf[256];
  int n = snprintf(buf, sizeof(buf),
                   "GiY mprotect old-space violation at %p phase=%s\n",
                   info->si_addr, g_old_guard_phase);
  if (n > 0) {
    ssize_t ignored = write(STDERR_FILENO, buf, (size_t) n);
    (void) ignored;
  }
  _exit(128 + SIGSEGV);
}

static void giy_old_guard_install_handler() {
  if (g_old_guard_handler_installed)
    return;

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = giy_old_guard_sigsegv;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO;
  if (sigaction(SIGSEGV, &sa, NULL) != 0) {
    printf("GiY sigaction failed errno=%d\n", errno);
    exit(1);
  }
  g_old_guard_handler_installed = true;
}

static void giy_old_guard_begin() {
  if (!giy_old_guard_enabled() || g_old_guard_active)
    return;
  if (dram_space.begin == 0 || dram_space.total_size == 0)
    return;

  if (g_old_guard_page_size == 0)
    g_old_guard_page_size = sysconf(_SC_PAGESIZE);
  if (g_old_guard_page_size <= 0) {
    printf("GiY old guard failed to read page size\n");
    exit(1);
  }

  giy_old_guard_install_handler();
  g_old_guard_phase = "begin";
  giy_old_guard_set_range((void *) dram_space.begin, dram_space.total_size, PROT_NONE);
  g_old_guard_active = true;
}

static void giy_old_guard_end() {
  if (!g_old_guard_active)
    return;
  giy_old_guard_set_range((void *) dram_space.begin,
                          dram_space.total_size,
                          PROT_READ | PROT_WRITE);
  g_old_guard_active = false;
  g_old_guard_phase = "inactive";
}

static inline void giy_old_guard_set_phase(const char *phase) {
  if (g_old_guard_active)
    g_old_guard_phase = phase;
}

static void giy_old_guard_allow_old_write(void *addr, size_t len) {
  if (!g_old_guard_active)
    return;
  giy_old_guard_set_range(addr, len, PROT_READ | PROT_WRITE);
}

static void giy_old_guard_protect_old_write(void *addr, size_t len) {
  if (!g_old_guard_active)
    return;
#if defined(__x86_64__) || defined(__i386__)
  _mm_sfence();
#endif
  giy_old_guard_set_range(addr, len, PROT_NONE);
}

template<typename Tracer>
static inline void giy_process_edge(JSValue &v) {
  Tracer::process_edge(v);
}

template<typename Tracer, typename T>
static inline void giy_process_edge(T *&p) {
  Tracer::process_edge(cast_process_edge_arg(p));
}

template<typename Tracer>
static void giy_scan_stack(JSValue *stack, int sp, int fp) {
  while (1) {
    while (sp >= fp) {
      Tracer::process_edge(stack[sp]);
      sp--;
    }
    if (sp < 0)
      return;
    fp = stack[sp--];
    Tracer::process_edge_function_frame(stack[sp--]);
    sp--;
    sp--;
  }
}

template<typename Tracer>
static void giy_scan_roots_generational(Context *ctx) {
  {
    struct global_constant_objects *gconstsp = &gconsts;
    JSValue *p;
    for (p = (JSValue *) gconstsp; p < (JSValue *) (gconstsp + 1); p++)
      Tracer::process_edge(*p);
  }
  {
    struct global_property_maps *gpmsp = &gpms;
    PropertyMap **p;
    for (p = (PropertyMap **) gpmsp; p < (PropertyMap **) (gpmsp + 1); p++)
      giy_process_edge<Tracer>(*p);
  }
  {
    struct global_object_shapes *gshapesp = &gshapes;
    Shape **p;
    for (p = (Shape **) gshapesp; p < (Shape **) (gshapesp + 1); p++)
      giy_process_edge<Tracer>(*p);
  }

  Tracer::process_edge(ctx->global);
  giy_process_edge<Tracer>(ctx->spreg.lp);
  Tracer::process_edge(ctx->spreg.a);
  Tracer::process_edge(ctx->spreg.err);
  if (ctx->exhandler_stack_top != NULL)
    giy_process_edge<Tracer>(ctx->exhandler_stack_top);
  Tracer::process_edge(ctx->lcall_stack);

  giy_scan_stack<Tracer>(ctx->stack, ctx->spreg.sp, ctx->spreg.fp);

  for (int i = 0; i < gc_root_stack_ptr; i++)
    Tracer::process_edge(*(gc_root_stack[i]));
}

static inline void giy_store_u64_old(uint64_t *slot, uint64_t bits) {
  if (GIY_PROFILE_DETAIL)
    giy_profile.old_slot_stream_stores++;
#if defined(__x86_64__)
  giy_old_guard_allow_old_write(slot, sizeof(*slot));
  _mm_stream_si64((long long *) slot, (long long) bits);
  g_used_nt_old_store = true;
  giy_old_guard_protect_old_write(slot, sizeof(*slot));
#else
  giy_old_guard_allow_old_write(slot, sizeof(*slot));
  *slot = bits;
  giy_old_guard_protect_old_write(slot, sizeof(*slot));
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
  if (g_gc_stack.in_cache_space && g_edge_log.in_cache_space &&
      g_ft_slot_set.in_cache_space)
    return;

  if (cache_space.work_begin == 0 || cache_space.end <= cache_space.work_begin) {
    printf("GiY stack bind failed: invalid cache work area\n");
    exit(1);
  }

  // Reserve a small fixed fraction of young area for GC auxiliaries.
  size_t young_bytes = (size_t) (cache_space.end - cache_space.work_begin);
  size_t stack_bytes = (young_bytes / 8) & ~(sizeof(uintptr_t) - 1);
  size_t edge_bytes = 0;
  size_t ft_slot_bytes = (young_bytes / 64) & ~(sizeof(uintptr_t) - 1);
  if (stack_bytes < 1024 * sizeof(uintptr_t))
    stack_bytes = 1024 * sizeof(uintptr_t);
  if (ft_slot_bytes < 4096 * sizeof(uintptr_t))
    ft_slot_bytes = 4096 * sizeof(uintptr_t);

  size_t reserve_total = stack_bytes + edge_bytes + ft_slot_bytes;
  if (reserve_total >= young_bytes) {
    printf("GiY aux bind failed: not enough cache bytes (%zu)\n", young_bytes);
    exit(1);
  }

  uintptr_t ft_slot_begin = cache_space.end - ft_slot_bytes;
  uintptr_t edge_begin = ft_slot_begin - edge_bytes;
  uintptr_t stack_begin = edge_begin - stack_bytes;
  cache_space.end = stack_begin;
  cache_space.total_size -= (int) reserve_total;

  g_gc_stack.items = (uintptr_t *) stack_begin;
  g_gc_stack.count = 0;
  g_gc_stack.capacity = stack_bytes / sizeof(uintptr_t);
  g_gc_stack.in_cache_space = true;

  g_edge_log.items = NULL;
  g_edge_log.count = 0;
  g_edge_log.capacity = 0;
  g_edge_log.in_cache_space = true;

  g_ft_slot_set.items = (uintptr_t *) ft_slot_begin;
  g_ft_slot_set.count = 0;
  g_ft_slot_set.capacity = ft_slot_bytes / sizeof(uintptr_t);
  g_ft_slot_set.in_cache_space = true;

  giy_profile.aux_stack_bytes = stack_bytes;
  giy_profile.aux_edge_bytes = edge_bytes;
  giy_profile.aux_ft_slot_bytes = ft_slot_bytes;
  giy_profile.aux_total_bytes = reserve_total;
  giy_profile.young_bytes_before_aux = young_bytes;
  giy_profile.young_bytes_after_aux =
    (size_t) (cache_space.end - cache_space.work_begin);
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
  if (GIY_PROFILE_DETAIL) {
    giy_profile.stack_pushes++;
    giy_profile_note_stack_depth();
  }
}

static inline bool gc_stack_empty() {
  return g_gc_stack.count == 0;
}

static inline uintptr_t gc_stack_pop() {
  return g_gc_stack.items[--g_gc_stack.count];
}

static inline void edge_log_reset() {
  g_edge_log.count = 0;
}

static inline bool giy_ft_value_is_young(JSValue value) {
  if (is_fixnum(value) || is_special(value))
    return false;
  return in_young_space((uintptr_t) clear_ptag(value));
}

static inline bool giy_ft_ptr_is_young(void *value) {
  return value != NULL && in_young_space((uintptr_t) value);
}

static void giy_ft_slot_set_add(uintptr_t raw_slot) {
  if (g_ft_slot_set.items == NULL) {
    printf("GiY function table slot set not bound to cache space\n");
    exit(1);
  }

  for (size_t i = 0; i < g_ft_slot_set.count; i++) {
    if (g_ft_slot_set.items[i] == raw_slot)
      return;
  }

  if (g_ft_slot_set.count >= g_ft_slot_set.capacity) {
    printf("GiY function table slot set overflow in cache space (capacity=%zu)\n",
           g_ft_slot_set.capacity);
    exit(1);
  }
  g_ft_slot_set.items[g_ft_slot_set.count++] = raw_slot;
  if (GIY_PROFILE_DETAIL) {
    giy_profile.ft_slots_recorded++;
    if (g_ft_slot_set.count > giy_profile.max_ft_slot_set)
      giy_profile.max_ft_slot_set = g_ft_slot_set.count;
  }
}

static void giy_traverse_stack_and_copy();
static inline uintptr_t forwarded_or_self(uintptr_t ptr);

template<typename Tracer>
static void giy_scan_jsobject_conservative(JSObject *p) {
  giy_process_edge<Tracer>(p->shape);

  object_header *hdr = ((object_header *) p) - 1;
  size_t eprop_offset = offsetof(JSObject, eprop);
  if ((size_t) hdr->size <= eprop_offset)
    return;

  size_t slots = ((size_t) hdr->size - eprop_offset) / sizeof(JSValue);
  for (size_t i = 0; i < slots; i++)
    Tracer::process_edge(p->eprop[i]);
}

template<typename Tracer>
static void giy_process_young_node(cell_type_t type, uintptr_t ptr) {
  switch (type) {
  case CELLT_SIMPLE_OBJECT:
  case CELLT_ARRAY:
  case CELLT_FUNCTION:
  case CELLT_BUILTIN:
  case CELLT_BOXED_NUMBER:
  case CELLT_BOXED_STRING:
  case CELLT_BOXED_BOOLEAN:
#ifdef USE_REGEXP
  case CELLT_REGEXP:
#endif
    giy_scan_jsobject_conservative<Tracer>((JSObject *) ptr);
    return;
  default:
    process_node<Tracer>(type, ptr);
    return;
  }
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
    uintptr_t to = forward(ptr);
    if (in_young_space((uintptr_t) &v)) {
      giy_profile_note_young_edge(EDGE_PATCH_JSVALUE_TAGGED);
      if (to != ptr) {
        v = put_ptag(to, get_ptag(v));
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_patched++;
      } else {
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_unforwarded_young++;
      }
    }
  }

  static void process_edge(void *&p) {
    uintptr_t ptr = (uintptr_t) p;
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    uintptr_t to = forward(ptr);
    if (in_young_space((uintptr_t) &p)) {
      giy_profile_note_young_edge(EDGE_PATCH_VOID_PTR);
      if (to != ptr) {
        p = (void *) to;
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_patched++;
      } else {
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_unforwarded_young++;
      }
    }
  }

  static void process_edge_function_frame(JSValue &v) {
    void *p = jsv_to_function_frame(v);
    uintptr_t ptr = (uintptr_t) p;
    if (ptr == 0 || !in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    uintptr_t to = forward(ptr);
    if (in_young_space((uintptr_t) &v)) {
      giy_profile_note_young_edge(EDGE_PATCH_FUNC_FRAME);
      if (to != ptr) {
        v = (JSValue) (uintjsv_t) to;
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_patched++;
      } else {
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_unforwarded_young++;
      }
    }
  }

  static void process_edge_ex_JSValue_array(JSValue *&array, size_t size) {
    (void) size;
    if (array == NULL)
      return;

    uintptr_t ptr = (uintptr_t) array;
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    uintptr_t to = forward(ptr);
    if (in_young_space((uintptr_t) &array)) {
      giy_profile_note_young_edge(EDGE_PATCH_JSVALUE_PTR);
      if (to != ptr) {
        array = (JSValue *) to;
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_patched++;
      } else {
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_unforwarded_young++;
      }
    }
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
    uintptr_t to = forward(ptr);
    if (in_young_space((uintptr_t) &array_ref)) {
      giy_profile_note_young_edge(EDGE_PATCH_JSVALUE_TAGGED);
      if (to != ptr) {
        array_ref = put_ptag(to, get_ptag(array_ref));
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_patched++;
      } else {
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_unforwarded_young++;
      }
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
    if (!in_young_space(ptr))
      return;

    pass_the_remember_set_count++;
    uintptr_t to = forward(ptr);
    if (in_young_space((uintptr_t) &array)) {
      giy_profile_note_young_edge(EDGE_PATCH_VOID_PTR);
      if (to != ptr) {
        array = (void **) to;
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_patched++;
      } else {
        if (GIY_PROFILE_DETAIL)
          giy_profile.edge_unforwarded_young++;
      }
    }
  }

  static void process_weak_edge(JSValue &v) { (void) v; }
  static void process_weak_edge(void *&p) { (void) p; }

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

  static void process_weak_edge(JSValue &v) { (void) v; }
  static void process_weak_edge(void *&p) { (void) p; }

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

class GiYWeakTracer {
private:
  static uintptr_t forward_and_materialize(uintptr_t ptr) {
    if (in_dram_space(ptr) || in_init_space(ptr) || !in_young_space(ptr))
      return ptr;

    object_header *hdr = (object_header *) ptr - 1;
    if (hdr->forwarding_pointer == 0) {
      (void) copy_for_minor(ptr);
      giy_traverse_stack_and_copy();
    }
    return hdr->forwarding_pointer;
  }

public:
  static constexpr bool is_single_object_scanner = false;
  static constexpr bool is_hcg_mutator = false;

  static void process_edge(JSValue &v) {
    if (is_fixnum(v) || is_special(v))
      return;

    uintptr_t ptr = (uintptr_t) clear_ptag(v);
    uintptr_t to = forward_and_materialize(ptr);
    if (to != ptr) {
      JSValue nv = put_ptag(to, get_ptag(v));
      bool slot_in_dram = in_dram_space((uintptr_t) &v);
      giy_store_jsvalue_slot(&v, nv, slot_in_dram);
    }
  }

  static void process_edge(void *&p) {
    uintptr_t ptr = (uintptr_t) p;
    uintptr_t to = forward_and_materialize(ptr);
    if (to != ptr) {
      bool slot_in_dram = in_dram_space((uintptr_t) &p);
      giy_store_ptr_slot(&p, (void *) to, slot_in_dram);
    }
  }

  static void process_edge_function_frame(JSValue &v) {
    void *p = jsv_to_function_frame(v);
    uintptr_t ptr = (uintptr_t) p;
    uintptr_t to = forward_and_materialize(ptr);
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
    uintptr_t to = forward_and_materialize(ptr);
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
    uintptr_t to = forward_and_materialize(ptr);
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

  static void process_edge_ex_ptr_array(void **&array, size_t size) {
    (void) size;
    if (array == NULL)
      return;

    uintptr_t ptr = (uintptr_t) array;
    uintptr_t to = forward_and_materialize(ptr);
    if (to != ptr) {
      bool slot_in_dram = in_dram_space((uintptr_t) &array);
      giy_store_ptr_slot((void **) &array, (void *) to, slot_in_dram);
    }
  }

  static void process_weak_edge(JSValue &v) { (void) v; }
  static void process_weak_edge(void *&p) { (void) p; }

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

  static void process_mark_stack() {
    giy_traverse_stack_and_copy();
  }
};

static void giy_reserve_edge(JSValue value) {
  if (is_fixnum(value) || is_special(value))
    return;

  uintptr_t ptr = (uintptr_t) clear_ptag(value);
  if (!in_young_space(ptr))
    return;

  pass_the_remember_set_count++;
  (void) copy_for_minor(ptr);
}

static void giy_reserve_edge(void *value) {
  uintptr_t ptr = (uintptr_t) value;
  if (!in_young_space(ptr))
    return;

  pass_the_remember_set_count++;
  (void) copy_for_minor(ptr);
}

#ifdef INLINE_CACHE
static bool giy_is_live_weak_jsvalue(JSValue v) {
  if (is_fixnum(v) || is_special(v))
    return true;
  return GiYWeakTracer::is_marked_cell((void *) clear_ptag(v));
}

static void giy_clear_inline_cache(InlineCache *ic) {
  ic->pm = NULL;
  ic->prop_name = JS_UNDEFINED;
}

static void giy_patch_live_inline_cache(Context *ctx) {
  for (int i = 0; i < FUNCTION_TABLE_LIMIT; i++) {
    FunctionTable *p = &ctx->function_table[i];
    for (int j = 0; j < p->n_insns; j++) {
      InlineCache *ic = &p->insns[j].inl_cache;
      if (ic->pm == NULL)
        continue;

      if (!GiYWeakTracer::is_marked_cell(ic->pm) ||
          !giy_is_live_weak_jsvalue(ic->prop_name)) {
        giy_clear_inline_cache(ic);
        continue;
      }

      GiYPatchTracer::process_edge(cast_process_edge_arg(ic->pm));
      GiYPatchTracer::process_edge(ic->prop_name);
    }
  }
}
#endif

static void giy_scan_remembered_set_slots() {
  for (int i = 0; i < remembered_set.count; i++) {
    if (GIY_PROFILE_DETAIL)
      giy_profile.rset_slots_scanned++;
    uintptr_t raw_slot = remembered_set.buffer[i];
    bool is_ptr_slot = (raw_slot & 1) != 0;
    uintptr_t slot_addr = raw_slot & ~(uintptr_t) 1;

    bool slot_in_dram = (slot_addr >= dram_space.begin && slot_addr < dram_space.end);
    bool slot_in_init = (slot_addr >= cache_space.begin && slot_addr < cache_space.work_begin);
    if (!slot_in_dram && !slot_in_init)
      continue;

    uintptr_t raw_value = remembered_set.values[i];
    if (raw_value == 0)
      continue;

    if (is_ptr_slot) {
      giy_reserve_edge((void *) raw_value);
    } else {
      giy_reserve_edge((JSValue) raw_value);
    }
  }
}

static void giy_reserve_function_table_slots() {
  for (size_t i = 0; i < g_ft_slot_set.count; i++) {
    if (GIY_PROFILE_DETAIL)
      giy_profile.ft_slots_scanned++;
    uintptr_t raw_slot = g_ft_slot_set.items[i];
    bool is_ptr_slot = (raw_slot & GIY_FT_PTR_SLOT_TAG) != 0;
    uintptr_t slot_addr = raw_slot & ~GIY_FT_PTR_SLOT_TAG;

    if (is_ptr_slot) {
      void **slot = (void **) slot_addr;
      void *value = *slot;
      giy_reserve_edge(value);
    } else {
      JSValue *slot = (JSValue *) slot_addr;
      JSValue value = *slot;
      giy_reserve_edge(value);
    }
  }
}

static void giy_patch_function_table_slots() {
  for (size_t i = 0; i < g_ft_slot_set.count; i++) {
    uintptr_t raw_slot = g_ft_slot_set.items[i];
    bool is_ptr_slot = (raw_slot & GIY_FT_PTR_SLOT_TAG) != 0;
    uintptr_t slot_addr = raw_slot & ~GIY_FT_PTR_SLOT_TAG;

    if (is_ptr_slot) {
      void **slot = (void **) slot_addr;
      void *old_value = *slot;
      void *new_value = old_value;
      GiYPatchTracer::process_edge(new_value);
      if (new_value != old_value) {
        *slot = new_value;
        if (GIY_PROFILE_DETAIL)
          giy_profile.ft_slots_patched++;
      }
    } else {
      JSValue *slot = (JSValue *) slot_addr;
      JSValue old_value = *slot;
      JSValue new_value = old_value;
      GiYPatchTracer::process_edge(new_value);
      if (new_value != old_value) {
        *slot = new_value;
        if (GIY_PROFILE_DETAIL)
          giy_profile.ft_slots_patched++;
      }
    }
  }
}

static inline void giy_clear_function_table_slots() {
  g_ft_slot_set.count = 0;
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

    uintptr_t raw_value = remembered_set.values[i];
    if (raw_value == 0)
      continue;

    if (is_ptr_slot) {
      void **slot = (void **) slot_addr;
      uintptr_t to = forwarded_or_self(raw_value);
      if (to != raw_value) {
        giy_store_ptr_slot(slot, (void *) to, slot_in_dram);
        if (GIY_PROFILE_DETAIL)
          giy_profile.rset_slots_patched++;
      }
    } else {
      JSValue *slot = (JSValue *) slot_addr;
      JSValue value = (JSValue) raw_value;
      if (is_fixnum(value) || is_special(value))
        continue;
      uintptr_t from = (uintptr_t) clear_ptag(value);
      uintptr_t to = forwarded_or_self(from);
      if (to != from) {
        giy_store_jsvalue_slot(slot, put_ptag(to, get_ptag(value)), slot_in_dram);
        if (GIY_PROFILE_DETAIL)
          giy_profile.rset_slots_patched++;
      }
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

// Copy a live object from young to old generation, using non-temporal stores when beneficial.
static inline void giy_copy_live_object(void *dst,
                                        const void *src,
                                        size_t nbytes,
                                        bool *used_nt_store) {
  giy_old_guard_allow_old_write(dst, nbytes);
  if (nbytes <= GIY_NT_COPY_MIN_BYTES) {
    memcpy(dst, src, nbytes);
    giy_old_guard_protect_old_write(dst, nbytes);
    return;
  }
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
  if (GIY_PROFILE_DETAIL) {
    giy_profile.nt_copy_objects++;
    giy_profile.nt_copy_bytes += nbytes;
  }
  giy_old_guard_protect_old_write(dst, nbytes);
  return;
#else
  // (void) used_nt_store;
#endif

  memcpy(dst, src, nbytes);
  giy_old_guard_protect_old_write(dst, nbytes);
}

static void giy_traverse_stack_and_copy() {
		bool used_nt_store = false;
		while (!gc_stack_empty()) {
			uintptr_t src_payload = gc_stack_pop();
			object_header *src_hdr = (object_header *) src_payload - 1;
      int align_bytes = ALIGN(src_hdr->size + sizeof(object_header));
      giy_profile_current_cell_type = (unsigned int) src_hdr->type;
      giy_profile_note_materialized(src_hdr->type, (size_t) align_bytes);

    // Traverse children and directly patch young in-object slots before memcpy.
    edge_log_reset();
    giy_process_young_node<GiYReserveTracer>(src_hdr->type, src_payload);

			uintptr_t dst_payload = src_hdr->forwarding_pointer;
		if (dst_payload == 0) {
			printf("GiY invariant violated: missing forwarding pointer\n");
			exit(1);
		}

			object_header *dst_hdr = ((object_header *) dst_payload) - 1;

    // Materialize in old generation with non-temporal stores when beneficial.
    giy_copy_live_object((void *) dst_hdr,
                         (const void *) src_hdr,
                         (size_t) align_bytes,
                         &used_nt_store);
		}
  giy_profile_current_cell_type = GIY_PROFILE_CELL_TYPES;

  if (used_nt_store)
    g_used_nt_old_store = true;
}

}  // namespace

void giy_print_profile() {
  printf("\n=== GiY Detailed Profile ===\n");
  if (!GIY_PROFILE_DETAIL) {
    printf("Detailed counters:   disabled (build with GIY_PROFILE_DETAIL=1 to enable)\n");
    printf("Young before aux:    %.2f KB\n",
           giy_profile.young_bytes_before_aux / 1024.0);
    printf("Young after aux:     %.2f KB\n",
           giy_profile.young_bytes_after_aux / 1024.0);
    printf("Aux total:           %.2f KB (stack %.2f KB, edge %.2f KB, ft %.2f KB)\n",
           giy_profile.aux_total_bytes / 1024.0,
           giy_profile.aux_stack_bytes / 1024.0,
           giy_profile.aux_edge_bytes / 1024.0,
           giy_profile.aux_ft_slot_bytes / 1024.0);
    printf("Small memcpy limit:  %zu bytes\n", GIY_NT_COPY_MIN_BYTES);
    return;
  }

  printf("Minor collections:   %llu\n", giy_profile.minor_collections);
  printf("Young before aux:    %.2f KB\n",
         giy_profile.young_bytes_before_aux / 1024.0);
  printf("Young after aux:     %.2f KB\n",
         giy_profile.young_bytes_after_aux / 1024.0);
  printf("Aux total:           %.2f KB (stack %.2f KB, edge %.2f KB, ft %.2f KB)\n",
         giy_profile.aux_total_bytes / 1024.0,
         giy_profile.aux_stack_bytes / 1024.0,
         giy_profile.aux_edge_bytes / 1024.0,
         giy_profile.aux_ft_slot_bytes / 1024.0);

  printf("Stack pushes:        %llu\n", giy_profile.stack_pushes);
  printf("Max stack depth:     %llu\n", giy_profile.max_stack_depth);

  printf("Young edge visits:   %llu\n", giy_profile.edge_entries);
  if (giy_profile.materialized_objects > 0) {
    printf("  Per object:        %.3f\n",
           (double) giy_profile.edge_entries /
             (double) giy_profile.materialized_objects);
  }
  if (giy_profile.minor_collections > 0) {
    printf("  Per minor GC:      %.3f\n",
           (double) giy_profile.edge_entries /
             (double) giy_profile.minor_collections);
  }
  printf("  Patched:           %llu\n", giy_profile.edge_patched);
  printf("  No-op old/init:    %llu\n", giy_profile.edge_noop);
  printf("  Immediate:         %llu\n", giy_profile.edge_immediate);
  printf("  Unforwarded young: %llu\n", giy_profile.edge_unforwarded_young);
  printf("  Max per object:    %llu\n", giy_profile.max_edge_log_per_object);
  printf("  Max per GC:        %llu\n", giy_profile.max_edge_log_per_gc);
  for (unsigned int i = 0; i < GIY_PROFILE_EDGE_KINDS; i++) {
    printf("  Kind %-15s %llu\n",
           giy_profile_edge_kind_name(i),
           giy_profile.edge_entries_by_kind[i]);
  }

  printf("Materialized objs:   %llu\n", giy_profile.materialized_objects);
  printf("Materialized bytes:  %.2f MB\n",
         giy_profile.materialized_bytes / (1024.0 * 1024.0));
  if (giy_profile.materialized_objects > 0) {
    printf("  Avg object bytes:  %.1f\n",
           (double) giy_profile.materialized_bytes /
             (double) giy_profile.materialized_objects);
  }
  printf("  <=64 bytes:        %llu\n", giy_profile.materialized_le_64);
  printf("  <=128 bytes:       %llu\n", giy_profile.materialized_le_128);
  printf("  <=256 bytes:       %llu\n", giy_profile.materialized_le_256);
  printf("  >256 bytes:        %llu\n", giy_profile.materialized_gt_256);
  printf("  Max bytes per GC:  %.2f KB\n",
         giy_profile.max_materialized_bytes_per_gc / 1024.0);
  printf("NT copy objects:     %llu\n", giy_profile.nt_copy_objects);
  printf("NT copy bytes:       %.2f MB\n",
         giy_profile.nt_copy_bytes / (1024.0 * 1024.0));
  printf("Old slot stores:     %llu\n", giy_profile.old_slot_stream_stores);

  printf("Function table slots recorded: %llu\n",
         giy_profile.ft_slots_recorded);
  printf("Function table max live set:   %llu\n",
         giy_profile.max_ft_slot_set);
  printf("Function table slots scanned:  %llu\n",
         giy_profile.ft_slots_scanned);
  printf("Function table slots patched:  %llu\n",
         giy_profile.ft_slots_patched);
  printf("Remembered set slots scanned:  %llu\n",
         giy_profile.rset_slots_scanned);
  printf("Remembered set slots patched:  %llu\n",
         giy_profile.rset_slots_patched);

  printf("Materialized by cell type:\n");
  for (unsigned int i = 0; i < GIY_PROFILE_CELL_TYPES; i++) {
    if (giy_profile.materialized_objects_by_type[i] == 0 &&
        giy_profile.edge_entries_by_type[i] == 0)
      continue;
    printf("  type %02u %-18s objs=%llu bytes=%.2f MB edge_entries=%llu\n",
           i,
           giy_profile_cell_type_name(i),
           giy_profile.materialized_objects_by_type[i],
           giy_profile.materialized_bytes_by_type[i] / (1024.0 * 1024.0),
           giy_profile.edge_entries_by_type[i]);
  }
}

void giy_bind_stack_to_cache() {
  giy_bind_stack_to_cache_impl();
}

extern "C" void giy_record_ft_jsvalue_slot(JSValue *slot, JSValue value) {
  if (in_minor_gc || slot == NULL || !giy_ft_value_is_young(value))
    return;
  giy_ft_slot_set_add((uintptr_t) slot);
}

extern "C" void giy_record_ft_ptr_slot(void **slot, void *value) {
  if (in_minor_gc || slot == NULL || !giy_ft_ptr_is_young(value))
    return;
  giy_ft_slot_set_add(((uintptr_t) slot) | GIY_FT_PTR_SLOT_TAG);
}

void giy_minor_collect(Context *ctx,
												 long long *scan_roots_ns,
												 long long *scan_rs_ns,
											 long long *young_trace_ns) {
		ensure_gc_stack_capacity();
  giy_profile_begin_minor_gc();
			gc_stack_reset();
  g_used_nt_old_store = false;
  giy_old_guard_begin();

	struct timespec t1, t2, t3, t4;
	clock_gettime(CLOCK_MONOTONIC, &t1);

  //phase 1: discover live objects from roots
  giy_old_guard_set_phase("scan_roots_reserve");
  giy_scan_roots_generational<GiYReserveTracer>(ctx);
  giy_old_guard_set_phase("function_table_reserve");
  giy_reserve_function_table_slots();
	clock_gettime(CLOCK_MONOTONIC, &t2);

#ifdef USE_REMEMBERED_SET
  //phase 2: discover live objects from remembered set slots
  giy_old_guard_set_phase("remembered_set_reserve");
	giy_scan_remembered_set_slots();
#endif
	clock_gettime(CLOCK_MONOTONIC, &t3);

  //phase 3: traverse the live young objects and copy them to old generation
  giy_old_guard_set_phase("young_traverse_copy");
	giy_traverse_stack_and_copy();

  //phase 4: patch the roots
  giy_old_guard_set_phase("scan_roots_patch");
  giy_scan_roots_generational<GiYPatchTracer>(ctx);
  giy_old_guard_set_phase("function_table_patch");
  giy_patch_function_table_slots();
#ifdef USE_REMEMBERED_SET
  //phase 5: patch the remembered set slots
  giy_old_guard_set_phase("remembered_set_patch");
  giy_patch_remembered_set_slots();
#endif
  giy_clear_function_table_slots();



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
  g_used_nt_old_store = false;
  // Generic weak_clear maintains old hidden-class/string metadata. Keeping it
  // correct requires old reads, so GiY ends the strict core guard here.
  giy_old_guard_set_phase("weak_clear_generic_old_metadata");
  giy_old_guard_end();
  weak_clear<GiYWeakTracer>(ctx);
#ifdef INLINE_CACHE
  giy_patch_live_inline_cache(ctx);
#endif
#if defined(__x86_64__) || defined(__i386__)
  if (g_used_nt_old_store)
    _mm_sfence();
#endif
}
