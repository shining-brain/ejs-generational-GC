#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/mman.h>
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "cache_dram_manager.h"
#include "gc-visitor-inl.h"

extern long pass_the_remember_set_count;
extern long long generational_forward_count;

namespace {

// Worklist that stores young payload pointers for minor traversal.
struct GiYGCStack {
  uintptr_t *items;
  size_t count;
  size_t head;
  size_t tail;
  size_t capacity;
  bool in_cache_space;
};

GiYGCStack g_gc_stack = {NULL, 0, 0, 0, 0, false};

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

struct GiYASUpdateEntry {
  AllocSite *as;
  PropertyMap *pm;
  int polymorphic;
};

struct GiYASUpdateLog {
  GiYASUpdateEntry *items;
  size_t count;
  size_t capacity;
};

struct GiYASObjectUpdateEntry {
  cell_type_t type;
  Shape *shape;
};

struct GiYASObjectUpdateLog {
  GiYASObjectUpdateEntry *items;
  size_t count;
  size_t capacity;
};

GiYASUpdateLog g_as_update_log = {NULL, 0, 0};
GiYASObjectUpdateLog g_as_object_update_log = {NULL, 0, 0};
bool g_as_object_update_applying = false;
bool g_used_nt_old_store = false;

static const uintptr_t GIY_FT_PTR_SLOT_TAG = 1;
static const unsigned int GIY_PROFILE_CELL_TYPES = 256;
static const unsigned int GIY_PROFILE_EDGE_KINDS = 4;
static const size_t GIY_NT_COPY_MIN_BYTES = 256;

#ifndef GIY_NT_COPY_BITS
#define GIY_NT_COPY_BITS 128
#endif

#if GIY_NT_COPY_BITS != 128 && GIY_NT_COPY_BITS != 256
#error "GIY_NT_COPY_BITS must be 128 or 256"
#endif

#ifndef GIY_PROFILE_DETAIL
#define GIY_PROFILE_DETAIL 0
#endif

#ifndef GIY_RSET_READ_SLOT_AT_GC
#define GIY_RSET_READ_SLOT_AT_GC 1
#endif

#ifndef GIY_GC_STACK_BYTES
#define GIY_GC_STACK_BYTES (48 * 1024)
#endif

#ifndef GIY_LOCAL_PADDING_BYTES
#define GIY_LOCAL_PADDING_BYTES 0
#endif

#ifndef GIY_AS_DRY_PROFILE
#define GIY_AS_DRY_PROFILE 0
#endif

#ifndef USE_GIYSB
#define USE_GIYSB 0
#endif

#ifndef GIYSB_TINY_BATCH
#define GIYSB_TINY_BATCH 1
#endif

#ifndef GIYSB_STAGING_BYTES
#define GIYSB_STAGING_BYTES (8 * 1024)
#endif

#ifndef GIYSB_SMALL_OBJECT_MAX_BYTES
#define GIYSB_SMALL_OBJECT_MAX_BYTES 256
#endif

#ifndef GIYSB_TINY_OBJECT_MAX_BYTES
#define GIYSB_TINY_OBJECT_MAX_BYTES 64
#endif

#ifndef GIYSB_TINY_TABLE_BYTES
#define GIYSB_TINY_TABLE_BYTES (64 * 1024)
#endif

#ifndef GIYSB_SMALL_OLD_RATIO
#define GIYSB_SMALL_OLD_RATIO 50
#endif

#ifndef GIYSB_PROFILE
#define GIYSB_PROFILE 0
#endif

#ifndef GIY_AS_UPDATE
#define GIY_AS_UPDATE 0
#endif

#ifndef GIY_AS_FT_ADVANCE
#define GIY_AS_FT_ADVANCE 1
#endif

#ifndef GIY_JSOBJECT_PRECISE_SCAN
#define GIY_JSOBJECT_PRECISE_SCAN 0
#endif

#ifndef GIY_JSOBJECT_PRECISE_ARRAY
#define GIY_JSOBJECT_PRECISE_ARRAY 1
#endif

#ifndef GIY_JSOBJECT_SHAPE_ONLY_SCAN
#define GIY_JSOBJECT_SHAPE_ONLY_SCAN 0
#endif

#ifndef GIY_JSOBJECT_TYPE_AWARE_SCAN
#define GIY_JSOBJECT_TYPE_AWARE_SCAN 0
#endif

#ifndef GIY_JSOBJECT_TYPE_AWARE_ARRAY
#define GIY_JSOBJECT_TYPE_AWARE_ARRAY 0
#endif

#ifndef GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE
#define GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE 0
#endif

#ifndef GIY_JSOBJECT_ARRAY_FAST_PATH
#define GIY_JSOBJECT_ARRAY_FAST_PATH 0
#endif

#ifndef GIY_JSOBJECT_ARRAY_SKIP_SIZE
#define GIY_JSOBJECT_ARRAY_SKIP_SIZE 0
#endif

#ifndef GIY_JSOBJECT_PRECISE_MIN_SLOTS
#define GIY_JSOBJECT_PRECISE_MIN_SLOTS 0
#endif

#ifndef GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES
#define GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES 0
#endif

#ifndef GIYOL_BATCH_BYTES
#define GIYOL_BATCH_BYTES (128 * 1024)
#endif

#ifndef GIYOL_SORT_BATCH
#define GIYOL_SORT_BATCH 0
#endif

#ifndef GIYOL_INLINE_COPY_ENTRIES
#define GIYOL_INLINE_COPY_ENTRIES 64
#endif

#ifndef GIYOL_WORKSPACE_BATCH_ENTRIES
#define GIYOL_WORKSPACE_BATCH_ENTRIES 4096
#endif

#ifndef GIYOL_ADAPTIVE_BATCH
#define GIYOL_ADAPTIVE_BATCH 0
#endif

#ifndef GIYOL_STAGING_COPY
#define GIYOL_STAGING_COPY 1
#endif

#ifndef GIYOL_TINY_STAGING
#define GIYOL_TINY_STAGING 1
#endif

#ifndef GIYOL_TINY_STAGING_BYTES
#define GIYOL_TINY_STAGING_BYTES (5 * 1024)
#endif

#ifndef GIYOL_TINY_FLUSH_BYTES
#define GIYOL_TINY_FLUSH_BYTES (4 * 1024)
#endif

#ifndef GIYOL_TINY_MAX_OBJECT_BYTES
#define GIYOL_TINY_MAX_OBJECT_BYTES 64
#endif

#ifndef GIYOL_FORCE_TINY_TAIL_NT
#define GIYOL_FORCE_TINY_TAIL_NT 0
#endif

#ifndef GIYOL_DIRECT_NONTINY_NT
#define GIYOL_DIRECT_NONTINY_NT 0
#endif

#ifndef GIYOL_DEFER_TINY_STAGING
#define GIYOL_DEFER_TINY_STAGING 1
#endif

struct GiYOLCopyEntry {
  void *dst;
  const void *src;
  size_t nbytes;
};

struct GiYOLCopyBatch {
  GiYOLCopyEntry *items;
  size_t count;
  size_t capacity;
  size_t bytes;
#if !GIYOL_STAGING_COPY
  GiYOLCopyEntry inline_items[GIYOL_INLINE_COPY_ENTRIES];
#endif
};

#if defined(USE_GIYOL) && GIYOL_ADAPTIVE_BATCH
static bool giyol_batch_next_traversal = false;
#endif

#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
static unsigned char *giyol_staging_buffer = NULL;
static size_t giyol_staging_capacity = 0;
static GiYOLCopyBatch *giyol_reserve_order_batch = NULL;
static bool giyol_reserve_order_batch_active = false;
static bool giyol_reserve_order_batch_overflow = false;
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
  unsigned long long giyol_batches;
  unsigned long long giyol_batch_objects;
  unsigned long long giyol_batch_bytes;
  unsigned long long giyol_small_flush_objects;
  unsigned long long giyol_small_flush_bytes;
  unsigned long long giyol_staging_batches;
  unsigned long long giyol_staging_objects;
  unsigned long long giyol_staging_bytes;
  unsigned long long giyol_staging_fallback_objects;
  unsigned long long giyol_staging_fallback_bytes;
  unsigned long long giyol_tiny_flushes;
  unsigned long long giyol_tiny_objects;
  unsigned long long giyol_tiny_bytes;
  unsigned long long giyol_tiny_fallback_objects;
  unsigned long long giyol_tiny_fallback_bytes;
  unsigned long long giyol_tiny_tail_nt_flushes;
  unsigned long long giyol_tiny_tail_nt_bytes;
  unsigned long long giyol_direct_nt_objects;
  unsigned long long giyol_direct_nt_bytes;
  unsigned long long old_slot_stream_stores;

  unsigned long long ft_slots_recorded;
  unsigned long long max_ft_slot_set;
  unsigned long long ft_slots_scanned;
  unsigned long long ft_slots_patched;

  unsigned long long rset_slots_scanned;
  unsigned long long rset_slots_patched;

  unsigned long long as_dry_objects;
  unsigned long long as_dry_pm_null;
  unsigned long long as_dry_pm_match;
  unsigned long long as_dry_pm_mismatch;
  unsigned long long as_update_observations;
  unsigned long long as_update_entries;
  unsigned long long as_update_applied;
  unsigned long long as_update_pm_changed;
  unsigned long long as_update_invalid_shape;
  unsigned long long as_update_invalid_pm;
  unsigned long long as_update_invalid_old_pm;
  unsigned long long as_update_lub_failed;
  unsigned long long as_shape_advance_attempts;
  unsigned long long as_shape_advance_changed;

  unsigned long long jsobject_scans;
  unsigned long long jsobject_precise_scans;
  unsigned long long jsobject_type_aware_scans;
  unsigned long long jsobject_precise_fallbacks;
  unsigned long long jsobject_conservative_scans;
  unsigned long long jsobject_conservative_slots;
  unsigned long long jsobject_precise_slots;
  unsigned long long jsobject_type_aware_slots;
  unsigned long long jsobject_skipped_slots;
  unsigned long long jsobject_shape_edges;
  unsigned long long jsobject_shape_layout_young;
  unsigned long long jsobject_pm_layout_young;
  unsigned long long jsobject_extension_arrays;
  unsigned long long jsobject_extension_slots;
  unsigned long long jsobject_layout_cache_hits;
  unsigned long long jsobject_layout_cache_misses;
  unsigned long long jsobject_layout_cache_invalid;

  size_t aux_stack_bytes;
  size_t aux_edge_bytes;
  size_t aux_ft_slot_bytes;
  size_t aux_jsobject_layout_cache_bytes;
  size_t aux_giysb_staging_bytes;
  size_t aux_giysb_tiny_table_bytes;
  size_t aux_giyol_staging_bytes;
  size_t aux_giyol_batch_bytes;
  size_t aux_padding_bytes;
  size_t aux_total_bytes;
  size_t young_bytes_before_aux;
  size_t young_bytes_after_aux;
};

GiYProfile giy_profile = {};
unsigned int giy_profile_current_cell_type = GIY_PROFILE_CELL_TYPES;

struct GiYJSObjectLayoutCacheEntry {
  Shape *shape;
  unsigned int epoch;
  unsigned int n_special_props;
  unsigned int actual_embedded;
  unsigned int extension_slots;
};

static GiYJSObjectLayoutCacheEntry *g_jsobject_layout_cache = NULL;
static size_t g_jsobject_layout_cache_capacity = 0;
static unsigned int g_jsobject_layout_cache_epoch = 1;

#if USE_GIYSB
struct GiYSBOldSpace {
  bool initialized;
  uintptr_t small_begin;
  uintptr_t small_free;
  uintptr_t small_end;
  uintptr_t large_begin;
  uintptr_t large_free;
  uintptr_t large_end;
};

struct GiYSBProfile {
  unsigned long long tiny_reserved_objects;
  unsigned long long tiny_reserved_bytes;
  unsigned long long tiny_overflow_objects;
  unsigned long long tiny_overflow_bytes;
  unsigned long long large_reserved_objects;
  unsigned long long large_reserved_bytes;
  unsigned long long staged_objects;
  unsigned long long staged_bytes;
  unsigned long long staging_flushes;
  unsigned long long staging_full_flushes;
  unsigned long long staging_tail_flushes;
  unsigned long long direct_tiny_objects;
  unsigned long long direct_tiny_bytes;
  unsigned long long max_tiny_table_entries_per_gc;
};

static GiYSBOldSpace g_giysb_old = {};
static GiYSBProfile g_giysb_profile = {};
static unsigned char *g_giysb_staging_buffer = NULL;
static uint32_t *g_giysb_tiny_table = NULL;
static size_t g_giysb_staging_capacity = 0;
static size_t g_giysb_tiny_table_capacity = 0;
static size_t g_giysb_tiny_table_count = 0;
static size_t g_giysb_staging_used = 0;
static uintptr_t g_giysb_staging_dst = 0;
static uintptr_t g_giysb_staging_expected = 0;
static uintptr_t g_giysb_tiny_batch_begin = 0;
#endif

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

#if USE_GIYSB
static inline size_t giysb_align_down(size_t bytes) {
  return bytes & ~(sizeof(uintptr_t) - 1);
}

static void giysb_ensure_old_space() {
  if (g_giysb_old.initialized)
    return;
  if (dram_space.begin == 0 || dram_space.total_size == 0) {
    printf("GiYSB old-space split failed: DRAM space is not initialized\n");
    exit(1);
  }
  if (GIYSB_SMALL_OLD_RATIO <= 0 || GIYSB_SMALL_OLD_RATIO >= 100) {
    printf("GiYSB small old ratio must be between 1 and 99 (current=%d)\n",
           GIYSB_SMALL_OLD_RATIO);
    exit(1);
  }

  size_t small_bytes =
    giysb_align_down((dram_space.total_size * (size_t) GIYSB_SMALL_OLD_RATIO) / 100);
  if (small_bytes == 0 || small_bytes >= dram_space.total_size) {
    printf("GiYSB old-space split failed: small_bytes=%zu total=%zu\n",
           small_bytes, dram_space.total_size);
    exit(1);
  }

  g_giysb_old.small_begin = dram_space.begin;
  g_giysb_old.small_free = g_giysb_old.small_begin;
  g_giysb_old.small_end = g_giysb_old.small_begin + small_bytes;
  g_giysb_old.large_begin = g_giysb_old.small_end;
  g_giysb_old.large_free = g_giysb_old.large_begin;
  g_giysb_old.large_end = dram_space.end;
  g_giysb_old.initialized = true;
  dram_space.available_bytes =
    (size_t) (g_giysb_old.small_end - g_giysb_old.small_free) +
    (size_t) (g_giysb_old.large_end - g_giysb_old.large_free);
  dram_space.free = g_giysb_old.large_free;

  printf("init_info: GiYSB old split small=%zuKB large=%zuKB ratio=%d%%\n",
         (size_t) (g_giysb_old.small_end - g_giysb_old.small_begin) / 1024,
         (size_t) (g_giysb_old.large_end - g_giysb_old.large_begin) / 1024,
         GIYSB_SMALL_OLD_RATIO);
}

static inline void giysb_refresh_dram_available() {
  dram_space.available_bytes =
    (size_t) (g_giysb_old.small_end - g_giysb_old.small_free) +
    (size_t) (g_giysb_old.large_end - g_giysb_old.large_free);
  dram_space.free = g_giysb_old.large_free;
}

static inline void giysb_tiny_table_reset() {
  g_giysb_tiny_table_count = 0;
  g_giysb_tiny_batch_begin = 0;
}

static bool giysb_tiny_table_append(uintptr_t payload_ptr,
                                    size_t align_bytes,
                                    object_header *dest_hdr) {
  if (g_giysb_tiny_table == NULL ||
      g_giysb_tiny_table_count >= g_giysb_tiny_table_capacity)
    return false;
  if (payload_ptr < cache_space.work_begin)
    return false;

  uintptr_t offset = payload_ptr - cache_space.work_begin;
  if ((offset & (sizeof(uintptr_t) - 1)) != 0)
    return false;
  size_t offset_units = offset / sizeof(uintptr_t);
  size_t size_class = align_bytes / sizeof(uintptr_t);
  if (size_class == 0 || size_class > 15 || (offset_units >> 28) != 0)
    return false;

  if (g_giysb_tiny_table_count == 0)
    g_giysb_tiny_batch_begin = (uintptr_t) dest_hdr;

  g_giysb_tiny_table[g_giysb_tiny_table_count++] =
    (uint32_t) ((offset_units << 4) | size_class);
#if GIYSB_PROFILE
  if (g_giysb_tiny_table_count >
      g_giysb_profile.max_tiny_table_entries_per_gc) {
    g_giysb_profile.max_tiny_table_entries_per_gc =
      g_giysb_tiny_table_count;
  }
#endif
  return true;
}

static object_header *giysb_reserve_old_object(uintptr_t payload_ptr,
                                               size_t align_bytes,
                                               bool *deferred_tiny) {
  giysb_ensure_old_space();
  *deferred_tiny = false;

#if GIYSB_TINY_BATCH
  if (align_bytes <= (size_t) GIYSB_TINY_OBJECT_MAX_BYTES) {
    if (g_giysb_old.small_free + align_bytes > g_giysb_old.small_end) {
      printf("GiYSB tiny old space full (%zu bytes)\n", align_bytes);
      exit(1);
    }
    if (g_giysb_tiny_table == NULL ||
        g_giysb_tiny_table_count >= g_giysb_tiny_table_capacity) {
      printf("GiYSB tiny table full (count=%zu capacity=%zu)\n",
             g_giysb_tiny_table_count, g_giysb_tiny_table_capacity);
      exit(1);
    }
    object_header *dest_hdr = (object_header *) g_giysb_old.small_free;
    if (!giysb_tiny_table_append(payload_ptr, align_bytes, dest_hdr)) {
      printf("GiYSB tiny table append failed\n");
      exit(1);
    }
    g_giysb_old.small_free += align_bytes;
    giysb_refresh_dram_available();
    *deferred_tiny = true;
#if GIYSB_PROFILE
    g_giysb_profile.tiny_reserved_objects++;
    g_giysb_profile.tiny_reserved_bytes += align_bytes;
#endif
    return dest_hdr;
  }
#else
  if (align_bytes <= (size_t) GIYSB_SMALL_OBJECT_MAX_BYTES &&
      g_giysb_old.small_free + align_bytes <= g_giysb_old.small_end) {
    object_header *dest_hdr = (object_header *) g_giysb_old.small_free;
    g_giysb_old.small_free += align_bytes;
    giysb_refresh_dram_available();
#if GIYSB_PROFILE
    g_giysb_profile.tiny_reserved_objects++;
    g_giysb_profile.tiny_reserved_bytes += align_bytes;
#endif
    return dest_hdr;
  }
#endif

  if (g_giysb_old.large_free + align_bytes <= g_giysb_old.large_end) {
    object_header *dest_hdr = (object_header *) g_giysb_old.large_free;
    g_giysb_old.large_free += align_bytes;
    giysb_refresh_dram_available();
#if GIYSB_PROFILE
    if (align_bytes <= (size_t) GIYSB_TINY_OBJECT_MAX_BYTES) {
      g_giysb_profile.tiny_overflow_objects++;
      g_giysb_profile.tiny_overflow_bytes += align_bytes;
    } else {
      g_giysb_profile.large_reserved_objects++;
      g_giysb_profile.large_reserved_bytes += align_bytes;
    }
#endif
    return dest_hdr;
  }

  printf("DRAM space full in GiYSB reserve (%zu bytes)\n", align_bytes);
  exit(1);
}

static inline bool giysb_is_tiny_destination(const void *dst, size_t nbytes) {
  uintptr_t ptr = (uintptr_t) dst;
  return nbytes <= (size_t) GIYSB_TINY_OBJECT_MAX_BYTES &&
         g_giysb_old.initialized &&
         ptr >= g_giysb_old.small_begin &&
         ptr + nbytes <= g_giysb_old.small_end;
}
#endif

static bool giy_payload_has_type_for_scan(const void *value, cell_type_t type) {
  uintptr_t ptr = (uintptr_t) value;
  if (ptr == 0 || (ptr & (sizeof(uintptr_t) - 1)) != 0)
    return false;
  if (!in_young_space(ptr) && !in_dram_space(ptr) && !in_init_space(ptr))
    return false;

  object_header *hdr = (object_header *) ptr - 1;
  return hdr->type == type;
}

static inline bool giy_scan_valid_shape(Shape *shape) {
  return giy_payload_has_type_for_scan(shape, CELLT_SHAPE);
}

static inline bool giy_scan_valid_property_map(PropertyMap *pm) {
  return giy_payload_has_type_for_scan(pm, CELLT_PROPERTY_MAP);
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

static void giy_old_guard_allow_old_read(void *addr, size_t len) {
  if (!g_old_guard_active)
    return;
  giy_old_guard_set_range(addr, len, PROT_READ);
}

static void giy_old_guard_protect_old_read(void *addr, size_t len) {
  if (!g_old_guard_active)
    return;
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

static inline void *giy_load_ptr_slot(void **slot, bool slot_in_dram) {
  if (slot_in_dram)
    giy_old_guard_allow_old_read(slot, sizeof(*slot));
  void *value = *slot;
  if (slot_in_dram)
    giy_old_guard_protect_old_read(slot, sizeof(*slot));
  return value;
}

static inline JSValue giy_load_jsvalue_slot(JSValue *slot, bool slot_in_dram) {
  if (slot_in_dram)
    giy_old_guard_allow_old_read(slot, sizeof(*slot));
  JSValue value = *slot;
  if (slot_in_dram)
    giy_old_guard_protect_old_read(slot, sizeof(*slot));
  return value;
}

static inline void giy_finish_local_nt_stores(bool *used_nt_store) {
#if defined(__x86_64__) || defined(__i386__)
  if (*used_nt_store)
    _mm_sfence();
#endif
  *used_nt_store = false;
}

static void giy_bind_stack_to_cache_impl() {
#if USE_GIYSB && GIYSB_TINY_BATCH
  if (g_gc_stack.in_cache_space && g_edge_log.in_cache_space &&
      g_ft_slot_set.in_cache_space && g_giysb_staging_buffer != NULL &&
      g_giysb_tiny_table != NULL
#if GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES > 0
      && g_jsobject_layout_cache != NULL
#endif
      )
    return;
#elif defined(USE_GIYOL) && GIYOL_STAGING_COPY
  if (g_gc_stack.in_cache_space && g_edge_log.in_cache_space &&
      g_ft_slot_set.in_cache_space && giyol_reserve_order_batch_active &&
      giyol_reserve_order_batch != NULL && giyol_staging_buffer != NULL
#if GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES > 0
      && g_jsobject_layout_cache != NULL
#endif
      )
    return;
#else
  if (g_gc_stack.in_cache_space && g_edge_log.in_cache_space &&
      g_ft_slot_set.in_cache_space
#if GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES > 0
      && g_jsobject_layout_cache != NULL
#endif
      )
    return;
#endif

  if (cache_space.work_begin == 0 || cache_space.end <= cache_space.work_begin) {
    printf("GiY stack bind failed: invalid cache work area\n");
    exit(1);
  }

  // Reserve GC Local Workspace from the young-side cache budget.
  size_t young_bytes = (size_t) (cache_space.end - cache_space.work_begin);
#if GIY_GC_STACK_BYTES > 0
  size_t stack_bytes = ALIGN((size_t) GIY_GC_STACK_BYTES);
#else
  size_t stack_bytes = (young_bytes / 8) & ~(sizeof(uintptr_t) - 1);
#endif
  size_t edge_bytes = 0;
  size_t ft_slot_bytes = ALIGN((size_t) GC_FT_SLOT_SET_BYTES);
#if GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES > 0
  size_t jsobject_layout_cache_entries =
    (size_t) GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES;
  size_t jsobject_layout_cache_bytes =
    ALIGN(jsobject_layout_cache_entries *
          sizeof(GiYJSObjectLayoutCacheEntry));
#else
  size_t jsobject_layout_cache_entries = 0;
  size_t jsobject_layout_cache_bytes = 0;
#endif
#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
  size_t giyol_staging_bytes =
    ALIGN((size_t) GIYOL_TINY_STAGING_BYTES);
  size_t giyol_batch_entries = (size_t) GIYOL_WORKSPACE_BATCH_ENTRIES;
  size_t giyol_batch_header_bytes = ALIGN(sizeof(GiYOLCopyBatch));
  size_t giyol_batch_item_bytes =
    ALIGN(giyol_batch_entries * sizeof(GiYOLCopyEntry));
  size_t giyol_batch_bytes =
    giyol_batch_header_bytes + giyol_batch_item_bytes;
#else
  size_t giyol_staging_bytes = 0;
  size_t giyol_batch_bytes = 0;
#endif
#if USE_GIYSB && GIYSB_TINY_BATCH
  size_t giysb_staging_bytes = ALIGN((size_t) GIYSB_STAGING_BYTES);
  size_t giysb_tiny_table_bytes = ALIGN((size_t) GIYSB_TINY_TABLE_BYTES);
#else
  size_t giysb_staging_bytes = 0;
  size_t giysb_tiny_table_bytes = 0;
#endif
  size_t padding_bytes = ALIGN((size_t) GIY_LOCAL_PADDING_BYTES);
  if (stack_bytes < 1024 * sizeof(uintptr_t))
    stack_bytes = 1024 * sizeof(uintptr_t);
  if (ft_slot_bytes == 0) {
    printf("GiY FT slot set size must be greater than zero\n");
    exit(1);
  }
#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
  if (giyol_batch_entries == 0) {
    printf("GiYOL workspace batch entries must be greater than zero\n");
    exit(1);
  }
  if (giyol_staging_bytes < (size_t) GIYOL_TINY_FLUSH_BYTES) {
    printf("GiYOL staging workspace too small (%zu < flush %zu)\n",
           giyol_staging_bytes, (size_t) GIYOL_TINY_FLUSH_BYTES);
    exit(1);
  }
#endif
#if USE_GIYSB && GIYSB_TINY_BATCH
  if (giysb_staging_bytes < (size_t) GIYSB_TINY_OBJECT_MAX_BYTES) {
    printf("GiYSB staging workspace too small (%zu < tiny object max %zu)\n",
           giysb_staging_bytes, (size_t) GIYSB_TINY_OBJECT_MAX_BYTES);
    exit(1);
  }
  if (giysb_tiny_table_bytes < sizeof(uint32_t)) {
    printf("GiYSB tiny table workspace too small (%zu)\n",
           giysb_tiny_table_bytes);
    exit(1);
  }
#endif

  size_t reserve_total = stack_bytes + edge_bytes + ft_slot_bytes +
                         jsobject_layout_cache_bytes +
                         giysb_staging_bytes + giysb_tiny_table_bytes +
                         giyol_staging_bytes + giyol_batch_bytes +
                         padding_bytes;
  if (reserve_total >= young_bytes) {
    printf("GiY aux bind failed: not enough cache bytes (%zu)\n", young_bytes);
    exit(1);
  }

  uintptr_t cursor = cache_space.end;
  cursor -= ft_slot_bytes;
  uintptr_t ft_slot_begin = cursor;
  cursor -= edge_bytes;
  uintptr_t edge_begin = cursor;
  (void) edge_begin;
  cursor -= stack_bytes;
  uintptr_t stack_begin = cursor;
#if GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES > 0
  cursor -= jsobject_layout_cache_bytes;
  uintptr_t jsobject_layout_cache_begin = cursor;
#endif
#if USE_GIYSB && GIYSB_TINY_BATCH
  cursor -= giysb_staging_bytes;
  uintptr_t giysb_staging_begin = cursor;
  cursor -= giysb_tiny_table_bytes;
  uintptr_t giysb_tiny_table_begin = cursor;
#endif
#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
  cursor -= giyol_staging_bytes;
  uintptr_t giyol_staging_begin = cursor;
  cursor -= giyol_batch_header_bytes;
  uintptr_t giyol_batch_header_begin = cursor;
  cursor -= giyol_batch_item_bytes;
  uintptr_t giyol_batch_items_begin = cursor;
#endif
  cursor -= padding_bytes;
  cache_space.end = cursor;
  cache_space.total_size -= (int) reserve_total;

  g_gc_stack.items = (uintptr_t *) stack_begin;
  g_gc_stack.count = 0;
  g_gc_stack.head = 0;
  g_gc_stack.tail = 0;
  g_gc_stack.capacity = stack_bytes / sizeof(uintptr_t);
  g_gc_stack.in_cache_space = true;
  printf("init_info: giy gc stack bytes=%zuKB capacity=%zu mode=%s\n",
         stack_bytes / 1024,
         g_gc_stack.capacity,
         GIY_GC_STACK_BYTES > 0 ? "fixed" : "young/8");

  g_edge_log.items = NULL;
  g_edge_log.count = 0;
  g_edge_log.capacity = 0;
  g_edge_log.in_cache_space = true;

  g_ft_slot_set.items = (uintptr_t *) ft_slot_begin;
  g_ft_slot_set.count = 0;
  g_ft_slot_set.capacity = ft_slot_bytes / sizeof(uintptr_t);
  g_ft_slot_set.in_cache_space = true;
  printf("init_info: giy ft slot set bytes=%zuKB capacity=%zu\n",
         ft_slot_bytes / 1024, g_ft_slot_set.capacity);

#if GIY_JSOBJECT_LAYOUT_CACHE_ENTRIES > 0
  g_jsobject_layout_cache =
    (GiYJSObjectLayoutCacheEntry *) jsobject_layout_cache_begin;
  g_jsobject_layout_cache_capacity = jsobject_layout_cache_entries;
  g_jsobject_layout_cache_epoch = 1;
  memset(g_jsobject_layout_cache, 0, jsobject_layout_cache_bytes);
#else
  (void) jsobject_layout_cache_entries;
#endif

#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
  giyol_staging_buffer = (unsigned char *) giyol_staging_begin;
  giyol_staging_capacity = giyol_staging_bytes;
  giyol_reserve_order_batch = (GiYOLCopyBatch *) giyol_batch_header_begin;
  giyol_reserve_order_batch->items =
    (GiYOLCopyEntry *) giyol_batch_items_begin;
  giyol_reserve_order_batch->count = 0;
  giyol_reserve_order_batch->capacity = giyol_batch_entries;
  giyol_reserve_order_batch->bytes = 0;
  giyol_reserve_order_batch_active = true;
  giyol_reserve_order_batch_overflow = false;
#endif

#if USE_GIYSB && GIYSB_TINY_BATCH
  g_giysb_staging_buffer = (unsigned char *) giysb_staging_begin;
  g_giysb_tiny_table = (uint32_t *) giysb_tiny_table_begin;
  g_giysb_staging_capacity = giysb_staging_bytes;
  g_giysb_tiny_table_capacity = giysb_tiny_table_bytes / sizeof(uint32_t);
  g_giysb_tiny_table_count = 0;
  g_giysb_staging_used = 0;
  g_giysb_staging_dst = 0;
  g_giysb_staging_expected = 0;
  g_giysb_tiny_batch_begin = 0;
  printf("init_info: GiYSB staging-only bytes=%zuKB tiny_table=%zuKB tiny_capacity=%zu tiny_max=%zu old_small_ratio=%d%% worklist=LIFO\n",
         giysb_staging_bytes / 1024,
         giysb_tiny_table_bytes / 1024,
         g_giysb_tiny_table_capacity,
         (size_t) GIYSB_TINY_OBJECT_MAX_BYTES,
         GIYSB_SMALL_OLD_RATIO);
#elif USE_GIYSB
  g_giysb_staging_buffer = NULL;
  g_giysb_tiny_table = NULL;
  g_giysb_staging_capacity = 0;
  g_giysb_tiny_table_capacity = 0;
  g_giysb_tiny_table_count = 0;
  g_giysb_staging_used = 0;
  g_giysb_staging_dst = 0;
  g_giysb_staging_expected = 0;
  g_giysb_tiny_batch_begin = 0;
  printf("init_info: GiYSB place-only tiny_batch=0 small_max=%zu old_small_ratio=%d%% worklist=LIFO\n",
         (size_t) GIYSB_SMALL_OBJECT_MAX_BYTES,
         GIYSB_SMALL_OLD_RATIO);
#endif

  giy_profile.aux_stack_bytes = stack_bytes;
  giy_profile.aux_edge_bytes = edge_bytes;
  giy_profile.aux_ft_slot_bytes = ft_slot_bytes;
  giy_profile.aux_jsobject_layout_cache_bytes = jsobject_layout_cache_bytes;
  giy_profile.aux_giysb_staging_bytes = giysb_staging_bytes;
  giy_profile.aux_giysb_tiny_table_bytes = giysb_tiny_table_bytes;
  giy_profile.aux_giyol_staging_bytes = giyol_staging_bytes;
  giy_profile.aux_giyol_batch_bytes = giyol_batch_bytes;
  giy_profile.aux_padding_bytes = padding_bytes;
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
  g_gc_stack.head = 0;
  g_gc_stack.tail = 0;
}

static inline void giy_jsobject_layout_cache_begin_minor_gc() {
  if (g_jsobject_layout_cache == NULL || g_jsobject_layout_cache_capacity == 0)
    return;

  g_jsobject_layout_cache_epoch++;
  if (g_jsobject_layout_cache_epoch == 0) {
    memset(g_jsobject_layout_cache, 0,
           g_jsobject_layout_cache_capacity *
             sizeof(GiYJSObjectLayoutCacheEntry));
    g_jsobject_layout_cache_epoch = 1;
  }
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
static uintptr_t copy_for_minor(uintptr_t payload_ptr);

#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
static bool giy_managed_payload_has_type(void *value, cell_type_t type) {
  uintptr_t ptr = (uintptr_t) value;
  if (ptr == 0 || (ptr & (sizeof(uintptr_t) - 1)) != 0)
    return false;
  if (!in_young_space(ptr) && !in_dram_space(ptr) && !in_init_space(ptr))
    return false;

  object_header *hdr = (object_header *) ptr - 1;
  return hdr->type == type;
}

static bool giy_valid_property_map(PropertyMap *pm) {
  return giy_managed_payload_has_type(pm, CELLT_PROPERTY_MAP);
}

static bool giy_valid_shape(Shape *shape) {
  return giy_managed_payload_has_type(shape, CELLT_SHAPE);
}

static bool giy_valid_hash_table(HashTable *map) {
  return giy_managed_payload_has_type(map, CELLT_HASHTABLE);
}

static PropertyMap *giy_find_lub(PropertyMap *a, PropertyMap *b) {
  if (a == NULL)
    return b;
  if (b == NULL)
    return a;
  if (!giy_valid_property_map(a) || !giy_valid_property_map(b))
    return NULL;

  while (!GC_PM_EQ(a, b) && a->prev != NULL) {
    if (a->n_props < b->n_props) {
      if (b->prev == NULL || !giy_valid_property_map(b->prev))
        return NULL;
      b = b->prev;
    } else {
      if (!giy_valid_property_map(a->prev))
        return NULL;
      a = a->prev;
    }
  }
  return a;
}

static void giy_as_update_log_reset() {
  g_as_update_log.count = 0;
}

static GiYASUpdateEntry *giy_as_update_entry_for(AllocSite *as) {
  for (size_t i = 0; i < g_as_update_log.count; i++) {
    if (g_as_update_log.items[i].as == as)
      return &g_as_update_log.items[i];
  }

  if (g_as_update_log.count >= g_as_update_log.capacity) {
    size_t new_capacity =
      g_as_update_log.capacity == 0 ? 1024 : g_as_update_log.capacity * 2;
    GiYASUpdateEntry *new_items =
      (GiYASUpdateEntry *) realloc(g_as_update_log.items,
                                   new_capacity * sizeof(GiYASUpdateEntry));
    if (new_items == NULL) {
      printf("GiY allocation-site update log allocation failed (capacity=%zu)\n",
             new_capacity);
      exit(1);
    }
    g_as_update_log.items = new_items;
    g_as_update_log.capacity = new_capacity;
  }

  GiYASUpdateEntry *entry = &g_as_update_log.items[g_as_update_log.count++];
  entry->as = as;
  if (giy_valid_property_map(as->pm)) {
    entry->pm = as->pm;
  } else {
    if (as->pm != NULL)
      giy_profile.as_update_invalid_old_pm++;
    entry->pm = NULL;
  }
  entry->polymorphic = as->polymorphic;
  giy_profile.as_update_entries++;
  return entry;
}

static void giy_as_update_entry_observe(GiYASUpdateEntry *entry,
                                        PropertyMap *pm) {
  if (pm == NULL)
    return;
  if (entry->pm != NULL && GC_PM_EQ(pm, entry->pm))
    return;

  if (entry->pm == NULL) {
    entry->pm = pm;
    return;
  }

  PropertyMap *lub = giy_find_lub(pm, entry->pm);
  if (lub == NULL) {
    giy_profile.as_update_lub_failed++;
    return;
  }
  if (GC_PM_EQ(lub, entry->pm)) {
    if (!entry->polymorphic)
      entry->pm = pm;
  } else if (!GC_PM_EQ(lub, pm)) {
    entry->polymorphic = 1;
    entry->pm = lub;
  }
}

static void giy_record_alloc_site_shape(Shape *shape) {
  if (shape == NULL || !giy_valid_shape(shape)) {
    giy_profile.as_update_invalid_shape++;
    return;
  }
  if (shape->alloc_site == NULL)
    return;
  if (!giy_valid_property_map(shape->pm)) {
    giy_profile.as_update_invalid_pm++;
    return;
  }

  giy_profile.as_update_observations++;
  GiYASUpdateEntry *entry = giy_as_update_entry_for(shape->alloc_site);
  giy_as_update_entry_observe(entry, shape->pm);
}
#else
static void giy_as_update_log_reset() {}
#endif

static Shape *giy_materialize_shape(Shape *shape) {
  if (shape == NULL)
    return NULL;

  uintptr_t ptr = (uintptr_t) shape;
  if (!in_young_space(ptr))
    return shape;

  (void) copy_for_minor(ptr);
  giy_traverse_stack_and_copy();
  return (Shape *) forwarded_or_self(ptr);
}

static PropertyMap *giy_materialize_property_map(PropertyMap *pm) {
  if (pm == NULL)
    return NULL;

  uintptr_t ptr = (uintptr_t) pm;
  if (!in_young_space(ptr))
    return pm;

  (void) copy_for_minor(ptr);
  giy_traverse_stack_and_copy();
  return (PropertyMap *) forwarded_or_self(ptr);
}

#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
static bool giy_cell_type_has_jsobject_shape(cell_type_t type) {
  switch (type) {
  case CELLT_SIMPLE_OBJECT:
#if !(GIY_JSOBJECT_PRECISE_SCAN && !GIY_JSOBJECT_PRECISE_ARRAY)
  case CELLT_ARRAY:
#endif
  case CELLT_FUNCTION:
  case CELLT_BUILTIN:
  case CELLT_BOXED_NUMBER:
  case CELLT_BOXED_STRING:
  case CELLT_BOXED_BOOLEAN:
#ifdef USE_REGEXP
  case CELLT_REGEXP:
#endif
    return true;
  default:
    return false;
  }
}

static void giy_as_object_update_log_reset() {
  g_as_object_update_log.count = 0;
}

static void giy_record_materialized_jsobject_for_as_update(cell_type_t type,
                                                          uintptr_t source_payload) {
  if (!giy_cell_type_has_jsobject_shape(type))
    return;

  if (g_as_object_update_log.count >= g_as_object_update_log.capacity) {
    size_t new_capacity =
      g_as_object_update_log.capacity == 0 ? 1024 :
                                             g_as_object_update_log.capacity * 2;
    GiYASObjectUpdateEntry *new_items =
      (GiYASObjectUpdateEntry *) realloc(
        g_as_object_update_log.items,
        new_capacity * sizeof(GiYASObjectUpdateEntry));
    if (new_items == NULL) {
      printf("GiY allocation-site object update log allocation failed "
             "(capacity=%zu)\n",
             new_capacity);
      exit(1);
    }
    g_as_object_update_log.items = new_items;
    g_as_object_update_log.capacity = new_capacity;
  }

  GiYASObjectUpdateEntry *entry =
    &g_as_object_update_log.items[g_as_object_update_log.count++];
  entry->type = type;
  entry->shape = ((JSObject *) source_payload)->shape;
}

static void giy_apply_materialized_object_alloc_site_updates() {
  if (g_as_object_update_applying)
    return;

  g_as_object_update_applying = true;
  for (size_t i = 0; i < g_as_object_update_log.count; i++) {
    GiYASObjectUpdateEntry *entry = &g_as_object_update_log.items[i];
    if (!giy_cell_type_has_jsobject_shape(entry->type))
      continue;
    giy_record_alloc_site_shape(entry->shape);
  }
  g_as_object_update_log.count = 0;
  g_as_object_update_applying = false;
}

static Shape *giy_next_valid_shape(Shape *shape) {
  if (shape == NULL)
    return NULL;
  Shape *next = shape->next;
  if (next == NULL || !giy_valid_shape(next))
    return NULL;
  return next;
}

static void giy_advance_alloc_site_shape(AllocSite *as) {
  if (as == NULL || as->shape == NULL || !giy_valid_shape(as->shape))
    return;
  giy_profile.as_shape_advance_attempts++;

  Shape *os = giy_materialize_shape(as->shape);
  if (os == NULL || !giy_valid_shape(os))
    return;
  as->shape = os;

  while (((os->n_enter - os->n_leave) << 3) < os->n_enter) {
    PropertyMap *pm = os->pm;
    if (!giy_valid_property_map(pm) || !giy_valid_hash_table(pm->map))
      break;

    HashTransitionIterator iter = createHashTransitionIterator(pm->map);
    HashTransitionCell *cell;
    Shape *next_os = NULL;
    while (nextHashTransitionCell(pm->map, &iter, &cell) != FAIL) {
      PropertyMap *next_pm = hash_transition_cell_pm(cell);
      if (!giy_valid_property_map(next_pm))
        continue;
      for (Shape *candidate = next_pm->shapes;
           candidate != NULL && giy_valid_shape(candidate);
           candidate = giy_next_valid_shape(candidate)) {
        if (candidate->alloc_site == as) {
          if (next_os == NULL)
            next_os = candidate;
          else
            return;
        }
      }
    }
    if (next_os == NULL)
      break;
    os = next_os;
  }

  if (as->shape == os)
    return;

  giy_profile.as_shape_advance_changed++;
  PropertyMap *new_pm = giy_materialize_property_map(os->pm);
  if (new_pm == NULL || !giy_valid_property_map(new_pm))
    return;

#ifdef DUMP_HCG
  if (as->shape != NULL)
    as->shape->is_cached = 0;
#endif
  as->pm = new_pm;
  as->shape = NULL;

  for (Shape *candidate = new_pm->shapes;
       candidate != NULL && giy_valid_shape(candidate);
       candidate = giy_next_valid_shape(candidate)) {
    if (candidate->alloc_site == as &&
        candidate->n_embedded_slots == new_pm->n_props) {
      Shape *materialized = giy_materialize_shape(candidate);
      if (materialized != NULL && giy_valid_shape(materialized)) {
        as->shape = materialized;
#ifdef DUMP_HCG
        as->shape->is_cached = 1;
#endif
      }
      break;
    }
  }
}
#endif

static void giy_apply_alloc_site_updates() {
#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
  for (size_t i = 0; i < g_as_update_log.count; i++) {
    GiYASUpdateEntry *entry = &g_as_update_log.items[i];
    AllocSite *as = entry->as;
    PropertyMap *new_pm = giy_materialize_property_map(entry->pm);
    if (new_pm == NULL)
      continue;

    bool old_pm_valid = giy_valid_property_map(as->pm);
    bool pm_changed = !old_pm_valid || !GC_PM_EQ(as->pm, new_pm);
    bool poly_changed = as->polymorphic != entry->polymorphic;
    if (!pm_changed && !poly_changed)
      continue;

    giy_profile.as_update_applied++;
    if (pm_changed)
      giy_profile.as_update_pm_changed++;
#ifdef DUMP_HCG
    if (as->shape != NULL)
      as->shape->is_cached = 0;
#endif
    as->pm = new_pm;
    as->polymorphic = entry->polymorphic;
    as->shape = NULL;
  }

#endif
}

#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE) && GIY_AS_FT_ADVANCE
static void giy_advance_function_table_alloc_sites(Context *ctx) {
  for (int i = 0; i < FUNCTION_TABLE_LIMIT; i++) {
    FunctionTable *p = &ctx->function_table[i];
    for (int j = 0; j < p->n_insns; j++) {
      AllocSite *as = &p->insns[j].alloc_site;
      giy_advance_alloc_site_shape(as);
    }
  }
}
#else
static void giy_advance_function_table_alloc_sites(Context *) {}
#endif

template<typename Tracer>
static void giy_scan_jsobject_conservative_body(JSObject *p, size_t slots) {
  giy_profile.jsobject_conservative_slots += slots;
  for (size_t i = 0; i < slots; i++)
    Tracer::process_edge(p->eprop[i]);
}

static size_t giy_jsobject_eprop_slots(JSObject *p) {
  object_header *hdr = ((object_header *) p) - 1;
  size_t eprop_offset = offsetof(JSObject, eprop);
  if ((size_t) hdr->size <= eprop_offset)
    return 0;
  return ((size_t) hdr->size - eprop_offset) / sizeof(JSValue);
}

template<typename Tracer>
static void giy_scan_jsobject_conservative(JSObject *p) {
#if GIY_AS_DRY_PROFILE && defined(ALLOC_SITE_CACHE)
  Shape *dry_shape = p->shape;
  if (dry_shape != NULL && dry_shape->alloc_site != NULL) {
    AllocSite *as = dry_shape->alloc_site;
    giy_profile.as_dry_objects++;
    if (as->pm == NULL)
      giy_profile.as_dry_pm_null++;
    else if (GC_PM_EQ(dry_shape->pm, as->pm))
      giy_profile.as_dry_pm_match++;
    else
      giy_profile.as_dry_pm_mismatch++;
  }
#endif

  giy_profile.jsobject_scans++;
  giy_profile.jsobject_conservative_scans++;
  giy_profile.jsobject_shape_edges++;
  giy_process_edge<Tracer>(p->shape);
  giy_scan_jsobject_conservative_body<Tracer>(p, giy_jsobject_eprop_slots(p));
}

template<typename Tracer>
static void giy_scan_jsarray_skip_size(JSObject *p) {
  giy_profile.jsobject_scans++;
  giy_profile.jsobject_type_aware_scans++;
  giy_profile.jsobject_shape_edges++;
  giy_process_edge<Tracer>(p->shape);

  size_t slots = giy_jsobject_eprop_slots(p);
  size_t visited_slots = 0;
  for (size_t i = array_body_index; i < slots; i++) {
    Tracer::process_edge(p->eprop[i]);
    visited_slots++;
  }

  giy_profile.jsobject_type_aware_slots += visited_slots;
  if (slots > visited_slots)
    giy_profile.jsobject_skipped_slots += slots - visited_slots;
}

#if GIY_JSOBJECT_TYPE_AWARE_SCAN
static size_t giy_jsobject_static_special_props(cell_type_t type) {
  switch (type) {
  case CELLT_ARRAY:
#if GIY_JSOBJECT_TYPE_AWARE_ARRAY
    return ARRAY_SPECIAL_PROPS;
#else
    return 0;
#endif
  case CELLT_FUNCTION:
    return FUNCTION_SPECIAL_PROPS;
  case CELLT_BUILTIN:
    return BUILTIN_SPECIAL_PROPS;
  case CELLT_BOXED_NUMBER:
#if GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE
    return NUMBER_SPECIAL_PROPS;
#else
    return 0;
#endif
  case CELLT_BOXED_STRING:
#if GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE
    return STRING_SPECIAL_PROPS;
#else
    return 0;
#endif
  case CELLT_BOXED_BOOLEAN:
    return BOOLEAN_SPECIAL_PROPS;
#ifdef USE_REGEXP
  case CELLT_REGEXP:
    return REX_SPECIAL_PROPS;
#endif
  default:
    return 0;
  }
}

template<typename Tracer>
static size_t giy_scan_jsobject_static_special_slots(cell_type_t type,
                                                    JSObject *p) {
  switch (type) {
#if GIY_JSOBJECT_TYPE_AWARE_ARRAY
  case CELLT_ARRAY: {
    JSValue *a_body = get_array_ptr_body(p);
    if (a_body != NULL) {
      size_t a_size = get_array_ptr_size(p);
      size_t a_length = (size_t) number_to_double(get_array_ptr_length(p));
      size_t len = a_length < a_size ? a_length : a_size;
      Tracer::process_edge_ex_JSValue_array(p->eprop[array_body_index], len);
      Tracer::process_edge(p->eprop[array_length_index]);
      giy_profile.jsobject_extension_arrays++;
      giy_profile.jsobject_extension_slots += len;
      return 2;
    }
    return 0;
  }
#endif
  case CELLT_FUNCTION:
    Tracer::process_edge(p->eprop[function_environment_index]);
    return 1;
#if GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE
  case CELLT_BOXED_NUMBER:
    Tracer::process_edge(p->eprop[number_object_value_index]);
    return 1;
  case CELLT_BOXED_STRING:
    Tracer::process_edge(p->eprop[string_object_value_index]);
    return 1;
#endif
  default:
    return 0;
  }
}

template<typename Tracer>
static void giy_scan_jsobject_type_aware(cell_type_t type, JSObject *p) {
  giy_profile.jsobject_scans++;

  size_t slots = giy_jsobject_eprop_slots(p);
  size_t special_props = giy_jsobject_static_special_props(type);

  giy_profile.jsobject_shape_edges++;
  giy_process_edge<Tracer>(p->shape);

  if (special_props == 0) {
    giy_profile.jsobject_conservative_scans++;
    giy_scan_jsobject_conservative_body<Tracer>(p, slots);
    return;
  }

  if (special_props > slots) {
    giy_profile.jsobject_precise_fallbacks++;
    giy_profile.jsobject_conservative_scans++;
    giy_scan_jsobject_conservative_body<Tracer>(p, slots);
    return;
  }

  giy_profile.jsobject_type_aware_scans++;
  size_t visited_slots =
    giy_scan_jsobject_static_special_slots<Tracer>(type, p);
  for (size_t i = special_props; i < slots; i++) {
    Tracer::process_edge(p->eprop[i]);
    visited_slots++;
  }

  giy_profile.jsobject_type_aware_slots += visited_slots;
  if (slots > visited_slots)
    giy_profile.jsobject_skipped_slots += slots - visited_slots;
}
#endif

#if GIY_JSOBJECT_PRECISE_SCAN
static size_t giy_jsobject_declared_special_props(cell_type_t type) {
  switch (type) {
  case CELLT_ARRAY:
    return ARRAY_SPECIAL_PROPS;
  case CELLT_FUNCTION:
    return FUNCTION_SPECIAL_PROPS;
  case CELLT_BUILTIN:
    return BUILTIN_SPECIAL_PROPS;
  case CELLT_BOXED_NUMBER:
    return NUMBER_SPECIAL_PROPS;
  case CELLT_BOXED_STRING:
    return STRING_SPECIAL_PROPS;
  case CELLT_BOXED_BOOLEAN:
    return BOOLEAN_SPECIAL_PROPS;
#ifdef USE_REGEXP
  case CELLT_REGEXP:
    return REX_SPECIAL_PROPS;
#endif
  default:
    return 0;
  }
}

static bool giy_jsobject_special_slots_ok(cell_type_t type, size_t slots) {
  switch (type) {
  case CELLT_ARRAY:
    return ARRAY_SPECIAL_PROPS <= slots &&
           array_body_index < slots &&
           array_length_index < slots;
  case CELLT_FUNCTION:
    return FUNCTION_SPECIAL_PROPS <= slots &&
           function_environment_index < slots;
  case CELLT_BOXED_NUMBER:
    return NUMBER_SPECIAL_PROPS <= slots &&
           number_object_value_index < slots;
  case CELLT_BOXED_STRING:
    return STRING_SPECIAL_PROPS <= slots &&
           string_object_value_index < slots;
  case CELLT_BOXED_BOOLEAN:
    return BOOLEAN_SPECIAL_PROPS <= slots &&
           boolean_object_value_index < slots;
  default:
    return true;
  }
}

static bool giy_jsobject_precise_layout(Shape *os,
                                        PropertyMap *pm,
                                        size_t slots,
                                        size_t *actual_embedded,
                                        size_t *extension_slots) {
  if (os == NULL || pm == NULL)
    return false;
  if (!giy_scan_valid_shape(os) || !giy_scan_valid_property_map(pm))
    return false;
  if (os->n_embedded_slots == 0 || os->n_embedded_slots > slots)
    return false;

  int n_extension = os->n_extension_slots;
  size_t embedded = os->n_embedded_slots -
    (n_extension == 0 ? 0 : 1);
  if (embedded > slots || pm->n_special_props > embedded)
    return false;
  if (n_extension != 0) {
    if (embedded >= slots || pm->n_props < embedded)
      return false;
    *extension_slots = pm->n_props - embedded;
  } else {
    *extension_slots = 0;
  }

  *actual_embedded = embedded;
  return true;
}

static bool giy_jsobject_layout_lookup(Shape *os,
                                       size_t slots,
                                       size_t *n_special_props,
                                       size_t *actual_embedded,
                                       size_t *extension_slots) {
  if (os == NULL)
    return false;

  if (g_jsobject_layout_cache != NULL &&
      g_jsobject_layout_cache_capacity > 0) {
    size_t index =
      (((uintptr_t) os) >> 4) % g_jsobject_layout_cache_capacity;
    GiYJSObjectLayoutCacheEntry *entry =
      &g_jsobject_layout_cache[index];
    if (entry->epoch == g_jsobject_layout_cache_epoch &&
        entry->shape == os) {
      if (entry->actual_embedded <= slots &&
          entry->n_special_props <= entry->actual_embedded &&
          (entry->extension_slots == 0 ||
           entry->actual_embedded < slots)) {
        giy_profile.jsobject_layout_cache_hits++;
        *n_special_props = entry->n_special_props;
        *actual_embedded = entry->actual_embedded;
        *extension_slots = entry->extension_slots;
        return true;
      }
      giy_profile.jsobject_layout_cache_invalid++;
      return false;
    }
    giy_profile.jsobject_layout_cache_misses++;
  }

  if (!giy_scan_valid_shape(os)) {
    giy_profile.jsobject_layout_cache_invalid++;
    return false;
  }

  PropertyMap *pm = os->pm;
  if (pm != NULL && in_young_space((uintptr_t) pm))
    giy_profile.jsobject_pm_layout_young++;

  size_t embedded = 0;
  size_t ext_slots = 0;
  if (!giy_jsobject_precise_layout(os, pm, slots, &embedded, &ext_slots)) {
    giy_profile.jsobject_layout_cache_invalid++;
    return false;
  }

  size_t special = pm->n_special_props;
  if (g_jsobject_layout_cache != NULL &&
      g_jsobject_layout_cache_capacity > 0) {
    if (special <= UINT_MAX &&
        embedded <= UINT_MAX &&
        ext_slots <= UINT_MAX) {
      size_t index =
        (((uintptr_t) os) >> 4) % g_jsobject_layout_cache_capacity;
      GiYJSObjectLayoutCacheEntry *entry =
        &g_jsobject_layout_cache[index];
      entry->shape = os;
      entry->epoch = g_jsobject_layout_cache_epoch;
      entry->n_special_props = (unsigned int) special;
      entry->actual_embedded = (unsigned int) embedded;
      entry->extension_slots = (unsigned int) ext_slots;
    }
  }

  *n_special_props = special;
  *actual_embedded = embedded;
  *extension_slots = ext_slots;
  return true;
}

template<typename Tracer>
static size_t giy_scan_jsobject_special_slots(cell_type_t type,
                                             JSObject *p) {
  switch (type) {
  case CELLT_ARRAY: {
    JSValue *a_body = get_array_ptr_body(p);
    if (a_body != NULL) {
      size_t a_size = get_array_ptr_size(p);
      size_t a_length = (size_t) number_to_double(get_array_ptr_length(p));
      size_t len = a_length < a_size ? a_length : a_size;
      Tracer::process_edge_ex_JSValue_array(p->eprop[array_body_index], len);
      Tracer::process_edge(p->eprop[array_length_index]);
      giy_profile.jsobject_extension_arrays++;
      giy_profile.jsobject_extension_slots += len;
      return 2;
    }
    return 0;
  }
  case CELLT_FUNCTION:
    Tracer::process_edge(p->eprop[function_environment_index]);
    return 1;
  case CELLT_BOXED_NUMBER:
    Tracer::process_edge(p->eprop[number_object_value_index]);
    return 1;
  case CELLT_BOXED_STRING:
    Tracer::process_edge(p->eprop[string_object_value_index]);
    return 1;
  case CELLT_BOXED_BOOLEAN:
    Tracer::process_edge(p->eprop[boolean_object_value_index]);
    return 1;
  default:
    return 0;
  }
}

template<typename Tracer>
static void giy_scan_jsobject_precise(cell_type_t type, JSObject *p) {
  giy_profile.jsobject_scans++;

  size_t slots = giy_jsobject_eprop_slots(p);
  Shape *layout_shape = p->shape;
  if (layout_shape != NULL && in_young_space((uintptr_t) layout_shape))
    giy_profile.jsobject_shape_layout_young++;

  giy_profile.jsobject_shape_edges++;
  giy_process_edge<Tracer>(p->shape);

  if (slots < (size_t) GIY_JSOBJECT_PRECISE_MIN_SLOTS) {
    giy_profile.jsobject_conservative_scans++;
    giy_scan_jsobject_conservative_body<Tracer>(p, slots);
    return;
  }

  size_t n_special_props = 0;
  size_t actual_embedded = 0;
  size_t extension_slots = 0;
#if GIY_JSOBJECT_SHAPE_ONLY_SCAN
  if (!giy_jsobject_special_slots_ok(type, slots) ||
      layout_shape == NULL ||
      !giy_scan_valid_shape(layout_shape) ||
      layout_shape->n_embedded_slots == 0 ||
      layout_shape->n_embedded_slots > slots ||
      layout_shape->n_extension_slots != 0) {
    giy_profile.jsobject_precise_fallbacks++;
    giy_profile.jsobject_conservative_scans++;
    giy_scan_jsobject_conservative_body<Tracer>(p, slots);
    return;
  }
  n_special_props = giy_jsobject_declared_special_props(type);
  actual_embedded = layout_shape->n_embedded_slots;
  extension_slots = 0;
  if (n_special_props > actual_embedded) {
    giy_profile.jsobject_precise_fallbacks++;
    giy_profile.jsobject_conservative_scans++;
    giy_scan_jsobject_conservative_body<Tracer>(p, slots);
    return;
  }
#else
  if (!giy_jsobject_special_slots_ok(type, slots) ||
      !giy_jsobject_layout_lookup(layout_shape, slots,
                                   &n_special_props,
                                   &actual_embedded, &extension_slots)) {
    giy_profile.jsobject_precise_fallbacks++;
    giy_profile.jsobject_conservative_scans++;
    giy_scan_jsobject_conservative_body<Tracer>(p, slots);
    return;
  }
#endif

  giy_profile.jsobject_precise_scans++;

  size_t visited_slots = giy_scan_jsobject_special_slots<Tracer>(type, p);
  for (size_t i = n_special_props; i < actual_embedded; i++) {
    Tracer::process_edge(p->eprop[i]);
    visited_slots++;
  }

  if (extension_slots != 0) {
    Tracer::process_edge_ex_JSValue_array(p->eprop[actual_embedded],
                                          extension_slots);
    giy_profile.jsobject_extension_arrays++;
    giy_profile.jsobject_extension_slots += extension_slots;
    visited_slots++;
  }

  giy_profile.jsobject_precise_slots += visited_slots;
  if (slots > visited_slots)
    giy_profile.jsobject_skipped_slots += slots - visited_slots;
}
#endif

template<typename Tracer>
static void giy_process_young_node(cell_type_t type, uintptr_t ptr) {
#if GIY_JSOBJECT_TYPE_AWARE_SCAN
  switch (type) {
  case CELLT_FUNCTION:
  case CELLT_BUILTIN:
  case CELLT_BOXED_BOOLEAN:
#ifdef USE_REGEXP
  case CELLT_REGEXP:
#endif
#if GIY_JSOBJECT_TYPE_AWARE_ARRAY
  case CELLT_ARRAY:
#endif
#if GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE
  case CELLT_BOXED_NUMBER:
  case CELLT_BOXED_STRING:
#endif
    giy_scan_jsobject_type_aware<Tracer>(type, (JSObject *) ptr);
    return;
  case CELLT_SIMPLE_OBJECT:
#if !GIY_JSOBJECT_TYPE_AWARE_ARRAY
  case CELLT_ARRAY:
#endif
#if !GIY_JSOBJECT_TYPE_AWARE_BOXED_PRIMITIVE
  case CELLT_BOXED_NUMBER:
  case CELLT_BOXED_STRING:
#endif
    giy_scan_jsobject_conservative<Tracer>((JSObject *) ptr);
    return;
  default:
    process_node<Tracer>(type, ptr);
    return;
  }
#else
#if GIY_JSOBJECT_ARRAY_FAST_PATH || \
    (GIY_JSOBJECT_PRECISE_SCAN && !GIY_JSOBJECT_PRECISE_ARRAY)
  if (__builtin_expect(type == CELLT_ARRAY, 1)) {
    giy_scan_jsobject_conservative<Tracer>((JSObject *) ptr);
    return;
  }
#endif
  switch (type) {
#if GIY_JSOBJECT_ARRAY_SKIP_SIZE
  case CELLT_ARRAY:
    giy_scan_jsarray_skip_size<Tracer>((JSObject *) ptr);
    return;
#endif
  case CELLT_SIMPLE_OBJECT:
#if !(GIY_JSOBJECT_ARRAY_SKIP_SIZE || GIY_JSOBJECT_ARRAY_FAST_PATH || \
      (GIY_JSOBJECT_PRECISE_SCAN && !GIY_JSOBJECT_PRECISE_ARRAY))
  case CELLT_ARRAY:
#endif
  case CELLT_FUNCTION:
  case CELLT_BUILTIN:
  case CELLT_BOXED_NUMBER:
  case CELLT_BOXED_STRING:
  case CELLT_BOXED_BOOLEAN:
#ifdef USE_REGEXP
  case CELLT_REGEXP:
#endif
#if GIY_JSOBJECT_TYPE_AWARE_SCAN
    giy_scan_jsobject_type_aware<Tracer>(type, (JSObject *) ptr);
#elif GIY_JSOBJECT_PRECISE_SCAN
    giy_scan_jsobject_precise<Tracer>(type, (JSObject *) ptr);
#else
    giy_scan_jsobject_conservative<Tracer>((JSObject *) ptr);
#endif
    return;
  default:
    process_node<Tracer>(type, ptr);
    return;
  }
#endif
}

#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
static void giyol_reserve_order_log_append(void *dst,
                                           const void *src,
                                           size_t nbytes);
#endif

// Reserve destination in old generation and mark source young object.
static uintptr_t copy_for_minor(uintptr_t payload_ptr) {
  object_header *hdr = (object_header *) payload_ptr - 1;

  if (hdr->forwarding_pointer != 0)
    return hdr->forwarding_pointer;

  int align_bytes = ALIGN(hdr->size + sizeof(object_header));
#if USE_GIYSB
  bool giysb_deferred_tiny = false;
  object_header *dest_hdr =
    giysb_reserve_old_object(payload_ptr,
                             (size_t) align_bytes,
                             &giysb_deferred_tiny);
  (void) giysb_deferred_tiny;
#else
  if (dram_space.available_bytes < (size_t) align_bytes) {
    printf("DRAM space full in copy_for_minor (%d bytes)\n", align_bytes);
    exit(1);
  }

  object_header *dest_hdr = (object_header *) dram_space.free;
  dram_space.free += align_bytes;
  dram_space.available_bytes -= align_bytes;
#endif

  hdr->forwarding_pointer = (uintptr_t) (dest_hdr + 1);
  gc_stack_push(payload_ptr);
#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
  if (!GIYOL_TINY_STAGING ||
      (size_t) align_bytes <= (size_t) GIYOL_TINY_MAX_OBJECT_BYTES) {
    giyol_reserve_order_log_append((void *) dest_hdr, (const void *) hdr,
                                   (size_t) align_bytes);
  }
#endif
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

#if GIY_RSET_READ_SLOT_AT_GC
    if (is_ptr_slot) {
      void **slot = (void **) slot_addr;
      void *value = giy_load_ptr_slot(slot, slot_in_dram);
      uintptr_t from = (uintptr_t) value;
      if (!in_young_space(from))
        continue;

      giy_reserve_edge(value);
      uintptr_t to = forwarded_or_self(from);
      if (to != from) {
        giy_store_ptr_slot(slot, (void *) to, slot_in_dram);
        if (GIY_PROFILE_DETAIL)
          giy_profile.rset_slots_patched++;
      }
    } else {
      JSValue *slot = (JSValue *) slot_addr;
      JSValue value = giy_load_jsvalue_slot(slot, slot_in_dram);
      if (is_fixnum(value) || is_special(value))
        continue;

      uintptr_t from = (uintptr_t) clear_ptag(value);
      if (!in_young_space(from))
        continue;

      giy_reserve_edge(value);
      uintptr_t to = forwarded_or_self(from);
      if (to != from) {
        giy_store_jsvalue_slot(slot, put_ptag(to, get_ptag(value)), slot_in_dram);
        if (GIY_PROFILE_DETAIL)
          giy_profile.rset_slots_patched++;
      }
    }
#else
    uintptr_t raw_value = remembered_set.values[i];
    if (raw_value == 0)
      continue;

    if (is_ptr_slot) {
      giy_reserve_edge((void *) raw_value);
    } else {
      giy_reserve_edge((JSValue) raw_value);
    }
#endif
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
#if GIY_RSET_READ_SLOT_AT_GC
  // In senior-style mode, the RSet scan already reads the current old slot,
  // reserves the young target, and patches that slot immediately.
  return;
#else
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
#endif
}

static inline uintptr_t forwarded_or_self(uintptr_t ptr) {
  if (!in_young_space(ptr))
    return ptr;

  object_header *hdr = (object_header *) ptr - 1;
  if (hdr->forwarding_pointer == 0)
    return ptr;
  return hdr->forwarding_pointer;
}

// Copy a live object from young to old generation. Normal GiY keeps the small
// memcpy cutoff; forced mode is used by GiYSB two-class materialization.
static inline void giy_copy_live_object_impl(void *dst,
                                             const void *src,
                                             size_t nbytes,
                                             bool *used_nt_store,
                                             bool force_nt_store) {
  giy_old_guard_allow_old_write(dst, nbytes);
  if (!force_nt_store && nbytes <= GIY_NT_COPY_MIN_BYTES) {
    memcpy(dst, src, nbytes);
    giy_old_guard_protect_old_write(dst, nbytes);
    return;
  }
#if defined(__x86_64__) || defined(__i386__)
  unsigned char *d = (unsigned char *) dst;
  const unsigned char *s = (const unsigned char *) src;
  size_t n = nbytes;

#if GIY_NT_COPY_BITS == 256 && defined(__AVX2__)
  if ((((uintptr_t) d) & 7) != 0) {
    if (force_nt_store) {
      printf("GiY forced NT copy alignment invariant failed (dst=%p, n=%zu)\n",
             (void *) d, n);
      exit(1);
    }
    memcpy(dst, src, nbytes);
    giy_old_guard_protect_old_write(dst, nbytes);
    return;
  }

  while ((((uintptr_t) d) & 31) != 0 && n >= 8) {
    if ((((uintptr_t) d) & 15) == 0 && n >= 16 &&
        (((uintptr_t) d) & 31) == 16) {
      __m128i v = _mm_loadu_si128((const __m128i *) s);
      _mm_stream_si128((__m128i *) d, v);
      s += 16;
      d += 16;
      n -= 16;
    } else {
      uint64_t v;
      memcpy(&v, s, sizeof(v));
      _mm_stream_si64((long long *) d, (long long) v);
      s += 8;
      d += 8;
      n -= 8;
    }
  }

  while (n >= 32) {
    __m256i v = _mm256_loadu_si256((const __m256i *) s);
    _mm256_stream_si256((__m256i *) d, v);
    s += 32;
    d += 32;
    n -= 32;
  }

  if (n >= 16) {
    __m128i v = _mm_loadu_si128((const __m128i *) s);
    _mm_stream_si128((__m128i *) d, v);
    s += 16;
    d += 16;
    n -= 16;
  }

  if (n >= 8) {
    uint64_t v;
    memcpy(&v, s, sizeof(v));
    _mm_stream_si64((long long *) d, (long long) v);
    s += 8;
    d += 8;
    n -= 8;
  }

  if (n != 0) {
    if (force_nt_store) {
      printf("GiY forced NT copy tail invariant failed (n=%zu)\n", n);
      exit(1);
    }
    memcpy(d, s, n);
  }
#else
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
#endif

  *used_nt_store = true;
  if (GIY_PROFILE_DETAIL) {
    giy_profile.nt_copy_objects++;
    giy_profile.nt_copy_bytes += nbytes;
  }
  giy_old_guard_protect_old_write(dst, nbytes);
  return;
#else
  if (force_nt_store) {
    printf("GiY forced NT copy is not supported on this architecture\n");
    exit(1);
  }
  // (void) used_nt_store;
#endif

  memcpy(dst, src, nbytes);
  giy_old_guard_protect_old_write(dst, nbytes);
}

static inline void giy_copy_live_object(void *dst,
                                        const void *src,
                                        size_t nbytes,
                                        bool *used_nt_store) {
  giy_copy_live_object_impl(dst, src, nbytes, used_nt_store, false);
}

static inline void giy_nt_copy_live_object_forced(void *dst,
                                                  const void *src,
                                                  size_t nbytes,
                                                  bool *used_nt_store) {
  giy_copy_live_object_impl(dst, src, nbytes, used_nt_store, true);
}

#if USE_GIYSB
static inline void giysb_staging_reset() {
  g_giysb_staging_used = 0;
  g_giysb_staging_dst = 0;
  g_giysb_staging_expected = 0;
}

static void giysb_flush_staging(bool *used_nt_store, bool full_flush) {
  if (g_giysb_staging_used == 0)
    return;

  giy_nt_copy_live_object_forced((void *) g_giysb_staging_dst,
                                 (const void *) g_giysb_staging_buffer,
                                 g_giysb_staging_used,
                                 used_nt_store);
#if GIYSB_PROFILE
  g_giysb_profile.staging_flushes++;
  if (full_flush)
    g_giysb_profile.staging_full_flushes++;
  else
    g_giysb_profile.staging_tail_flushes++;
#else
  (void) full_flush;
#endif
  giysb_staging_reset();
}

static bool giysb_decode_tiny_entry(uint32_t entry,
                                    object_header **src_hdr,
                                    size_t *nbytes) {
  size_t size_class = (size_t) (entry & 0xF);
  *nbytes = size_class * sizeof(uintptr_t);
  uintptr_t offset_units = (uintptr_t) (entry >> 4);
  uintptr_t src_payload =
    cache_space.work_begin + offset_units * sizeof(uintptr_t);
  *src_hdr = (object_header *) src_payload - 1;

  return *nbytes != 0 &&
         *nbytes <= (size_t) GIYSB_TINY_OBJECT_MAX_BYTES &&
         src_payload >= cache_space.work_begin &&
         src_payload < cache_space.end;
}

static void giysb_stage_tiny_chunk(size_t start,
                                   size_t end,
                                   uintptr_t dst_start,
                                   size_t chunk_bytes,
                                   bool *used_nt_store,
                                   bool full_flush) {
  if (g_giysb_staging_buffer == NULL ||
      chunk_bytes > g_giysb_staging_capacity) {
    printf("GiYSB staging buffer unavailable or too small (chunk=%zu capacity=%zu)\n",
           chunk_bytes, g_giysb_staging_capacity);
    exit(1);
    return;
  }

  giysb_staging_reset();
  uintptr_t dst_cursor = dst_start;
  size_t off = 0;
  for (size_t i = start; i < end; i++) {
    object_header *src_hdr = NULL;
    size_t nbytes = 0;
    if (!giysb_decode_tiny_entry(g_giysb_tiny_table[i],
                                 &src_hdr,
                                 &nbytes)) {
      printf("GiYSB tiny table entry invariant failed\n");
      exit(1);
    }

    memcpy(g_giysb_staging_buffer + off, (const void *) src_hdr, nbytes);
    off += nbytes;
    dst_cursor += nbytes;
#if GIYSB_PROFILE
    g_giysb_profile.staged_objects++;
    g_giysb_profile.staged_bytes += nbytes;
#endif
  }

  g_giysb_staging_dst = dst_start;
  g_giysb_staging_expected = dst_cursor;
  g_giysb_staging_used = off;
  giysb_flush_staging(used_nt_store, full_flush);
}

static void giysb_flush_tiny_chunk(size_t start,
                                   size_t end,
                                   uintptr_t dst_start,
                                   size_t chunk_bytes,
                                   bool *used_nt_store,
                                   bool full_flush) {
  if (chunk_bytes == 0 || start == end)
    return;

  giysb_stage_tiny_chunk(start,
                         end,
                         dst_start,
                         chunk_bytes,
                         used_nt_store,
                         full_flush);
}

static void giysb_flush_tiny_table(bool *used_nt_store) {
  if (g_giysb_tiny_table_count == 0)
    return;
  if (g_giysb_tiny_batch_begin == 0) {
    printf("GiYSB tiny flush invariant failed\n");
    exit(1);
  }

  giysb_staging_reset();
  size_t chunk_start = 0;
  uintptr_t chunk_dst = g_giysb_tiny_batch_begin;
  uintptr_t dst_cursor = g_giysb_tiny_batch_begin;
  size_t chunk_bytes = 0;

  for (size_t i = 0; i < g_giysb_tiny_table_count; i++) {
    object_header *src_hdr = NULL;
    size_t nbytes = 0;
    if (!giysb_decode_tiny_entry(g_giysb_tiny_table[i],
                                 &src_hdr,
                                 &nbytes)) {
      printf("GiYSB tiny table entry invariant failed\n");
      exit(1);
    }
    (void) src_hdr;

    if (g_giysb_staging_capacity != 0 &&
        chunk_bytes != 0 &&
        chunk_bytes + nbytes > g_giysb_staging_capacity) {
      giysb_flush_tiny_chunk(chunk_start,
                             i,
                             chunk_dst,
                             chunk_bytes,
                             used_nt_store,
                             true);
      chunk_start = i;
      chunk_dst = dst_cursor;
      chunk_bytes = 0;
    }

    chunk_bytes += nbytes;
    dst_cursor += nbytes;
  }

  giysb_flush_tiny_chunk(chunk_start,
                         g_giysb_tiny_table_count,
                         chunk_dst,
                         chunk_bytes,
                         used_nt_store,
                         false);
  giysb_tiny_table_reset();
}
#endif

#if defined(USE_GIYOL)
#if GIYOL_SORT_BATCH || GIYOL_STAGING_COPY
static int giyol_copy_entry_compare(const void *a, const void *b) {
  const GiYOLCopyEntry *x = (const GiYOLCopyEntry *) a;
  const GiYOLCopyEntry *y = (const GiYOLCopyEntry *) b;
  if (x->dst < y->dst)
    return -1;
  if (x->dst > y->dst)
    return 1;
  return 0;
}
#endif

static inline void giyol_stream_copy_region(void *dst,
                                            const void *src,
                                            size_t nbytes,
                                            bool *used_nt_store) {
  giy_old_guard_allow_old_write(dst, nbytes);
#if defined(__x86_64__) || defined(__i386__)
  unsigned char *d = (unsigned char *) dst;
  const unsigned char *s = (const unsigned char *) src;
  size_t n = nbytes;
  bool streamed = false;

  if ((((uintptr_t) d) & 7) != 0) {
    memcpy(dst, src, nbytes);
    giy_old_guard_protect_old_write(dst, nbytes);
    return;
  }

  if ((((uintptr_t) d) & 15) != 0) {
    if ((((uintptr_t) d) & 15) != 8 || n < 8) {
      memcpy(dst, src, nbytes);
      giy_old_guard_protect_old_write(dst, nbytes);
      return;
    }
    uint64_t v;
    memcpy(&v, s, sizeof(v));
    _mm_stream_si64((long long *) d, (long long) v);
    streamed = true;
    s += 8;
    d += 8;
    n -= 8;
  }

  while (n >= 16) {
    __m128i v = _mm_loadu_si128((const __m128i *) s);
    _mm_stream_si128((__m128i *) d, v);
    streamed = true;
    s += 16;
    d += 16;
    n -= 16;
  }

  if (n >= 8) {
    uint64_t v;
    memcpy(&v, s, sizeof(v));
    _mm_stream_si64((long long *) d, (long long) v);
    streamed = true;
    s += 8;
    d += 8;
    n -= 8;
  }

  if (n != 0) {
    memcpy(d, s, n);
  }

  if (streamed) {
    *used_nt_store = true;
  }
  if (streamed && GIY_PROFILE_DETAIL) {
    giy_profile.nt_copy_objects++;
    giy_profile.nt_copy_bytes += nbytes;
  }
#else
  memcpy(dst, src, nbytes);
#endif
  giy_old_guard_protect_old_write(dst, nbytes);
}

static inline void giyol_stream_copy_live_object(void *dst,
                                                const void *src,
                                                size_t nbytes,
                                                bool *used_nt_store) {
  giyol_stream_copy_region(dst, src, nbytes, used_nt_store);
}

static inline void giyol_direct_nt_copy_live_object(void *dst,
                                                   const void *src,
                                                   size_t nbytes,
                                                   bool *used_nt_store) {
  giyol_stream_copy_region(dst, src, nbytes, used_nt_store);
  giy_profile.giyol_direct_nt_objects++;
  giy_profile.giyol_direct_nt_bytes += nbytes;
}

#if !GIYOL_STAGING_COPY
static void giyol_copy_batch_init(GiYOLCopyBatch *batch) {
  batch->items = batch->inline_items;
  batch->capacity = GIYOL_INLINE_COPY_ENTRIES;
  batch->count = 0;
  batch->bytes = 0;
}

static void giyol_copy_batch_destroy(GiYOLCopyBatch *batch) {
  if (batch->items != batch->inline_items)
    free(batch->items);
  batch->items = batch->inline_items;
  batch->count = 0;
  batch->capacity = GIYOL_INLINE_COPY_ENTRIES;
  batch->bytes = 0;
}
#endif

static bool giyol_copy_batch_reserve_entry(GiYOLCopyBatch *batch) {
  if (batch->count < batch->capacity)
    return true;

#if GIYOL_STAGING_COPY
  return false;
#else
  size_t new_capacity = batch->capacity == 0 ?
                        GIYOL_INLINE_COPY_ENTRIES : batch->capacity * 2;
  GiYOLCopyEntry *new_items;
  if (batch->items == batch->inline_items) {
    new_items =
      (GiYOLCopyEntry *) malloc(new_capacity * sizeof(GiYOLCopyEntry));
    if (new_items != NULL)
      memcpy(new_items, batch->inline_items,
             batch->count * sizeof(GiYOLCopyEntry));
  } else {
    new_items =
      (GiYOLCopyEntry *) realloc(batch->items,
                                 new_capacity * sizeof(GiYOLCopyEntry));
  }
  if (new_items == NULL) {
    printf("GiYOL copy batch allocation failed (capacity=%zu)\n",
           new_capacity);
    exit(1);
  }
  batch->items = new_items;
  batch->capacity = new_capacity;
  return true;
#endif
}

static bool giyol_copy_batch_append(GiYOLCopyBatch *batch,
                                    void *dst,
                                    const void *src,
                                    size_t nbytes) {
  if (!giyol_copy_batch_reserve_entry(batch))
    return false;
  GiYOLCopyEntry *entry = &batch->items[batch->count++];
  entry->dst = dst;
  entry->src = src;
  entry->nbytes = nbytes;
  batch->bytes += nbytes;
  return true;
}

static inline void giyol_copy_batch_reset(GiYOLCopyBatch *batch) {
  batch->count = 0;
  batch->bytes = 0;
}

#if GIYOL_STAGING_COPY
static void giyol_reserve_order_batch_begin() {
  if (!giyol_reserve_order_batch_active || giyol_reserve_order_batch == NULL) {
    printf("GiYOL reserve-order batch is not bound to GC Local Workspace\n");
    exit(1);
  }
  giyol_reserve_order_batch->count = 0;
  giyol_reserve_order_batch->bytes = 0;
  giyol_reserve_order_batch_overflow = false;
}

static void giyol_reserve_order_log_append(void *dst,
                                           const void *src,
                                           size_t nbytes) {
  if (!giyol_reserve_order_batch_active)
    giyol_reserve_order_batch_begin();
  if (!giyol_copy_batch_append(giyol_reserve_order_batch, dst, src, nbytes))
    giyol_reserve_order_batch_overflow = true;
}

static void giyol_ensure_staging_capacity(size_t bytes) {
  if (bytes == 0)
    return;
  if (giyol_staging_capacity >= bytes)
    return;

  printf("GiYOL staging buffer overflow (needed=%zu, capacity=%zu)\n",
         bytes, giyol_staging_capacity);
  exit(1);
}

static void giyol_flush_staging_chunk(GiYOLCopyEntry *items,
                                      size_t start,
                                      size_t end,
                                      void *dst,
                                      size_t bytes,
                                      bool use_nt,
                                      bool *used_nt_store) {
  if (bytes == 0)
    return;

  if (use_nt) {
    giyol_ensure_staging_capacity(bytes);
    size_t off = 0;
    for (size_t i = start; i < end; i++) {
      memcpy(giyol_staging_buffer + off, items[i].src, items[i].nbytes);
      off += items[i].nbytes;
    }
    giyol_stream_copy_region(dst, giyol_staging_buffer, bytes, used_nt_store);
    giy_profile.giyol_batches++;
    giy_profile.giyol_batch_objects += (end - start);
    giy_profile.giyol_batch_bytes += bytes;
    giy_profile.giyol_staging_batches++;
    giy_profile.giyol_staging_objects += (end - start);
    giy_profile.giyol_staging_bytes += bytes;
  } else {
    for (size_t i = start; i < end; i++) {
      giy_copy_live_object(items[i].dst, items[i].src, items[i].nbytes,
                           used_nt_store);
      giy_profile.giyol_small_flush_objects++;
      giy_profile.giyol_small_flush_bytes += items[i].nbytes;
      giy_profile.giyol_staging_fallback_objects++;
      giy_profile.giyol_staging_fallback_bytes += items[i].nbytes;
    }
  }
}

#if GIYOL_TINY_STAGING
static void giyol_flush_tiny_chunk(GiYOLCopyEntry *items,
                                   size_t start,
                                   size_t end,
                                   void *dst,
                                   size_t bytes,
                                   bool force_nt,
                                   bool *used_nt_store) {
  if (bytes == 0)
    return;

  bool use_nt = force_nt &&
                (bytes >= (size_t) GIYOL_TINY_FLUSH_BYTES ||
                 GIYOL_FORCE_TINY_TAIL_NT);
  if (use_nt) {
    giyol_stream_copy_region(dst, giyol_staging_buffer, bytes, used_nt_store);
    giy_profile.giyol_batches++;
    giy_profile.giyol_batch_objects += (end - start);
    giy_profile.giyol_batch_bytes += bytes;
    giy_profile.giyol_staging_batches++;
    giy_profile.giyol_staging_objects += (end - start);
    giy_profile.giyol_staging_bytes += bytes;
    giy_profile.giyol_tiny_flushes++;
    giy_profile.giyol_tiny_objects += (end - start);
    giy_profile.giyol_tiny_bytes += bytes;
    if (bytes < (size_t) GIYOL_TINY_FLUSH_BYTES) {
      giy_profile.giyol_tiny_tail_nt_flushes++;
      giy_profile.giyol_tiny_tail_nt_bytes += bytes;
    }
  } else {
    for (size_t i = start; i < end; i++) {
      giy_copy_live_object(items[i].dst, items[i].src, items[i].nbytes,
                           used_nt_store);
      giy_profile.giyol_small_flush_objects++;
      giy_profile.giyol_small_flush_bytes += items[i].nbytes;
      giy_profile.giyol_staging_fallback_objects++;
      giy_profile.giyol_staging_fallback_bytes += items[i].nbytes;
      giy_profile.giyol_tiny_fallback_objects++;
      giy_profile.giyol_tiny_fallback_bytes += items[i].nbytes;
    }
  }
}

static void giyol_flush_tiny_staging_batch(GiYOLCopyBatch *batch,
                                           bool force_nt,
                                           bool *used_nt_store) {
  if (batch->count == 0)
    return;

  giyol_ensure_staging_capacity((size_t) GIYOL_TINY_STAGING_BYTES);

#if GIYOL_DEFER_TINY_STAGING
  size_t i = 0;
  while (i < batch->count) {
    GiYOLCopyEntry *entry = &batch->items[i];
    bool small = entry->nbytes <= (size_t) GIYOL_TINY_MAX_OBJECT_BYTES;

    if (!small) {
      giy_copy_live_object(entry->dst, entry->src, entry->nbytes,
                           used_nt_store);
      giy_profile.giyol_small_flush_objects++;
      giy_profile.giyol_small_flush_bytes += entry->nbytes;
      giy_profile.giyol_staging_fallback_objects++;
      giy_profile.giyol_staging_fallback_bytes += entry->nbytes;
      giy_profile.giyol_tiny_fallback_objects++;
      giy_profile.giyol_tiny_fallback_bytes += entry->nbytes;
      i++;
      continue;
    }

    size_t run_start = i;
    uintptr_t expected = (uintptr_t) entry->dst;
    while (i < batch->count) {
      GiYOLCopyEntry *run_entry = &batch->items[i];
      bool run_small =
        run_entry->nbytes <= (size_t) GIYOL_TINY_MAX_OBJECT_BYTES;
      uintptr_t dst = (uintptr_t) run_entry->dst;
      if (!run_small || dst != expected)
        break;
      expected += run_entry->nbytes;
      i++;
    }

    size_t pos = run_start;
    while (pos < i) {
      size_t chunk_start = pos;
      size_t chunk_end = pos;
      size_t bytes = 0;
      uintptr_t chunk_dst = (uintptr_t) batch->items[pos].dst;

      while (chunk_end < i &&
             bytes + batch->items[chunk_end].nbytes <=
             (size_t) GIYOL_TINY_STAGING_BYTES) {
        bytes += batch->items[chunk_end].nbytes;
        chunk_end++;
        if (bytes >= (size_t) GIYOL_TINY_FLUSH_BYTES)
          break;
      }

      bool use_nt = force_nt &&
                    (bytes >= (size_t) GIYOL_TINY_FLUSH_BYTES ||
                     GIYOL_FORCE_TINY_TAIL_NT);
      if (use_nt) {
        size_t off = 0;
        for (size_t j = chunk_start; j < chunk_end; j++) {
          memcpy(giyol_staging_buffer + off,
                 batch->items[j].src,
                 batch->items[j].nbytes);
          off += batch->items[j].nbytes;
        }
      }

      giyol_flush_tiny_chunk(batch->items, chunk_start, chunk_end,
                             (void *) chunk_dst, bytes, force_nt,
                             used_nt_store);
      pos = chunk_end;
    }
  }

  batch->count = 0;
  batch->bytes = 0;
  return;
#endif

  size_t chunk_start = 0;
  size_t chunk_end = 0;
  uintptr_t chunk_dst = 0;
  uintptr_t expected = 0;
  size_t off = 0;

#define GIYOL_TINY_FLUSH_CURRENT()                                           \
  do {                                                                       \
    if (off != 0) {                                                          \
      giyol_flush_tiny_chunk(batch->items, chunk_start, chunk_end,           \
                             (void *) chunk_dst, off, force_nt,              \
                             used_nt_store);                                 \
      off = 0;                                                               \
      chunk_start = chunk_end;                                               \
      chunk_dst = 0;                                                         \
      expected = 0;                                                          \
    }                                                                        \
  } while (0)

  for (size_t i = 0; i < batch->count; i++) {
    GiYOLCopyEntry *entry = &batch->items[i];
    uintptr_t dst = (uintptr_t) entry->dst;
    bool small = entry->nbytes <= (size_t) GIYOL_TINY_MAX_OBJECT_BYTES;

    if (!small) {
      GIYOL_TINY_FLUSH_CURRENT();
      giy_copy_live_object(entry->dst, entry->src, entry->nbytes,
                           used_nt_store);
      giy_profile.giyol_small_flush_objects++;
      giy_profile.giyol_small_flush_bytes += entry->nbytes;
      giy_profile.giyol_staging_fallback_objects++;
      giy_profile.giyol_staging_fallback_bytes += entry->nbytes;
      giy_profile.giyol_tiny_fallback_objects++;
      giy_profile.giyol_tiny_fallback_bytes += entry->nbytes;
      chunk_start = i + 1;
      chunk_end = i + 1;
      continue;
    }

    if (off != 0 && dst != expected) {
      GIYOL_TINY_FLUSH_CURRENT();
      chunk_start = i;
      chunk_end = i;
    }

    if (off == 0) {
      chunk_start = i;
      chunk_dst = dst;
      expected = dst;
    }

    if (off + entry->nbytes > (size_t) GIYOL_TINY_STAGING_BYTES) {
      GIYOL_TINY_FLUSH_CURRENT();
      chunk_start = i;
      chunk_dst = dst;
      expected = dst;
    }

    memcpy(giyol_staging_buffer + off, entry->src, entry->nbytes);
    off += entry->nbytes;
    expected += entry->nbytes;
    chunk_end = i + 1;

    if (off >= (size_t) GIYOL_TINY_FLUSH_BYTES) {
      GIYOL_TINY_FLUSH_CURRENT();
      chunk_start = i + 1;
      chunk_end = i + 1;
    }
  }

  GIYOL_TINY_FLUSH_CURRENT();
#undef GIYOL_TINY_FLUSH_CURRENT

  batch->count = 0;
  batch->bytes = 0;
}
#endif

static void giyol_flush_staging_batch(GiYOLCopyBatch *batch,
                                      bool force_nt,
                                      bool *used_nt_store) {
  if (batch->count == 0)
    return;

#if GIYOL_TINY_STAGING
  giyol_flush_tiny_staging_batch(batch, force_nt, used_nt_store);
  return;
#endif

  qsort(batch->items, batch->count, sizeof(GiYOLCopyEntry),
        giyol_copy_entry_compare);

  size_t i = 0;
  while (i < batch->count) {
    size_t chunk_start = i;
    uintptr_t chunk_dst = (uintptr_t) batch->items[i].dst;
    uintptr_t expected = chunk_dst;
    size_t chunk_bytes = 0;

    while (i < batch->count) {
      GiYOLCopyEntry *entry = &batch->items[i];
      uintptr_t dst = (uintptr_t) entry->dst;
      bool contiguous = dst == expected;
      if (!contiguous)
        break;
      expected += entry->nbytes;
      chunk_bytes += entry->nbytes;
      i++;
      if (chunk_bytes >= (size_t) GIYOL_BATCH_BYTES)
        break;
    }

    bool use_nt = force_nt && chunk_bytes >= (size_t) GIYOL_BATCH_BYTES;
    giyol_flush_staging_chunk(batch->items, chunk_start, i,
                              (void *) chunk_dst, chunk_bytes, use_nt,
                              used_nt_store);

    if (i < batch->count &&
        (uintptr_t) batch->items[i].dst != expected) {
      size_t gap_start = i;
      giy_copy_live_object(batch->items[i].dst, batch->items[i].src,
                           batch->items[i].nbytes, used_nt_store);
      giy_profile.giyol_small_flush_objects++;
      giy_profile.giyol_small_flush_bytes += batch->items[i].nbytes;
      giy_profile.giyol_staging_fallback_objects++;
      giy_profile.giyol_staging_fallback_bytes += batch->items[i].nbytes;
      i++;
      (void) gap_start;
    }
  }

  batch->count = 0;
  batch->bytes = 0;
}
#endif

static void giyol_flush_copy_batch(GiYOLCopyBatch *batch,
                                  bool force_nt,
                                  bool *used_nt_store) {
  if (batch->count == 0)
    return;

#if GIYOL_STAGING_COPY
  giyol_flush_staging_batch(batch, force_nt, used_nt_store);
  return;
#endif

#if GIYOL_SORT_BATCH
  qsort(batch->items, batch->count, sizeof(GiYOLCopyEntry),
        giyol_copy_entry_compare);
#endif

  bool use_nt = force_nt && batch->bytes >= (size_t) GIYOL_BATCH_BYTES;
  if (use_nt) {
    giy_profile.giyol_batches++;
    giy_profile.giyol_batch_objects += batch->count;
    giy_profile.giyol_batch_bytes += batch->bytes;
  } else {
    giy_profile.giyol_small_flush_objects += batch->count;
    giy_profile.giyol_small_flush_bytes += batch->bytes;
  }

  for (size_t i = 0; i < batch->count; i++) {
    GiYOLCopyEntry *entry = &batch->items[i];
    if (use_nt) {
      giyol_stream_copy_live_object(entry->dst, entry->src, entry->nbytes,
                                    used_nt_store);
    } else {
      giy_copy_live_object(entry->dst, entry->src, entry->nbytes,
                           used_nt_store);
    }
  }

  batch->count = 0;
  batch->bytes = 0;
}

#if !GIYOL_STAGING_COPY
static void giyol_enqueue_copy(GiYOLCopyBatch *batch,
                              void *dst,
                              const void *src,
                              size_t nbytes,
                              bool *used_nt_store) {
  (void) giyol_copy_batch_append(batch, dst, src, nbytes);

  if (!GIYOL_STAGING_COPY && batch->bytes >= (size_t) GIYOL_BATCH_BYTES)
    giyol_flush_copy_batch(batch, true, used_nt_store);
}
#endif
#endif

static void giy_traverse_stack_and_copy() {
		bool used_nt_store = false;
#if defined(USE_GIYOL)
    bool giyol_use_batch =
#if GIYOL_ADAPTIVE_BATCH
      giyol_batch_next_traversal;
#else
      true;
#endif
    size_t giyol_traversal_bytes = 0;
#if !GIYOL_STAGING_COPY
    GiYOLCopyBatch copy_batch;
    giyol_copy_batch_init(&copy_batch);
#endif
#endif
		while (!gc_stack_empty()) {
			uintptr_t src_payload = gc_stack_pop();
			object_header *src_hdr = (object_header *) src_payload - 1;
      cell_type_t type = src_hdr->type;
      int align_bytes = ALIGN(src_hdr->size + sizeof(object_header));
      giy_profile_current_cell_type = (unsigned int) type;
      giy_profile_note_materialized(type, (size_t) align_bytes);

    // Traverse children and directly patch young in-object slots before memcpy.
    edge_log_reset();
    giy_process_young_node<GiYReserveTracer>(type, src_payload);

			uintptr_t dst_payload = src_hdr->forwarding_pointer;
		if (dst_payload == 0) {
			printf("GiY invariant violated: missing forwarding pointer\n");
			exit(1);
		}

			object_header *dst_hdr = ((object_header *) dst_payload) - 1;

#if USE_GIYSB && GIYSB_TINY_BATCH
	    if (giysb_is_tiny_destination((const void *) dst_hdr,
	                                   (size_t) align_bytes)) {
	      // Tiny objects are materialized after the LIFO traversal finishes.
	    } else {
	      giy_nt_copy_live_object_forced((void *) dst_hdr,
	                                     (const void *) src_hdr,
	                                     (size_t) align_bytes,
	                                     &used_nt_store);
	    }
#elif defined(USE_GIYOL)
	    giyol_traversal_bytes += (size_t) align_bytes;
#if GIYOL_STAGING_COPY
	    if (!giyol_use_batch || giyol_reserve_order_batch_overflow ||
	        (GIYOL_TINY_STAGING &&
	         (size_t) align_bytes > (size_t) GIYOL_TINY_MAX_OBJECT_BYTES)) {
#if GIYOL_DIRECT_NONTINY_NT
      giyol_direct_nt_copy_live_object((void *) dst_hdr,
                                       (const void *) src_hdr,
                                       (size_t) align_bytes,
                                       &used_nt_store);
#else
      giy_copy_live_object((void *) dst_hdr,
                           (const void *) src_hdr,
                           (size_t) align_bytes,
                           &used_nt_store);
#endif
    }
#else
    if (giyol_use_batch) {
      // GiYOL keeps GiY traversal unchanged, but delays materialization until a
      // contiguous old-space batch is large enough for streaming stores.
      giyol_enqueue_copy(&copy_batch,
                         (void *) dst_hdr,
                         (const void *) src_hdr,
                         (size_t) align_bytes,
                         &used_nt_store);
    } else {
      // If recent survivor volume is too small to form a batch, preserve GiY's
      // immediate materialization path and avoid cache-locality side effects.
      giy_copy_live_object((void *) dst_hdr,
                           (const void *) src_hdr,
                           (size_t) align_bytes,
                           &used_nt_store);
    }
#endif
#else
    // Materialize in old generation with non-temporal stores when beneficial.
    giy_copy_live_object((void *) dst_hdr,
                         (const void *) src_hdr,
                         (size_t) align_bytes,
                         &used_nt_store);
#endif
#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
    giy_record_materialized_jsobject_for_as_update(type, src_payload);
#endif
		}
#if defined(USE_GIYOL)
#if GIYOL_STAGING_COPY
	  if (giyol_use_batch && !giyol_reserve_order_batch_overflow)
	    giyol_flush_copy_batch(giyol_reserve_order_batch, true, &used_nt_store);
	  else
	    giyol_copy_batch_reset(giyol_reserve_order_batch);
#else
  if (giyol_use_batch)
    giyol_flush_copy_batch(&copy_batch,
                           GIYOL_STAGING_COPY ? true : false,
                           &used_nt_store);
  giyol_copy_batch_destroy(&copy_batch);
#endif
#if GIYOL_ADAPTIVE_BATCH
  giyol_batch_next_traversal =
    giyol_traversal_bytes >= (size_t) GIYOL_BATCH_BYTES;
#else
	  (void) giyol_traversal_bytes;
#endif
#endif
#if USE_GIYSB && GIYSB_TINY_BATCH
  giysb_flush_tiny_table(&used_nt_store);
#endif
  giy_finish_local_nt_stores(&used_nt_store);
	  giy_profile_current_cell_type = GIY_PROFILE_CELL_TYPES;
#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
  if (!g_old_guard_active)
	  giy_apply_materialized_object_alloc_site_updates();
#endif
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
    printf("Aux total:           %.2f KB (stack %.2f KB, edge %.2f KB, ft %.2f KB, jsobj layout %.2f KB, giysb staging %.2f KB, giysb tiny table %.2f KB, giyol staging %.2f KB, giyol batch %.2f KB, padding %.2f KB)\n",
           giy_profile.aux_total_bytes / 1024.0,
           giy_profile.aux_stack_bytes / 1024.0,
           giy_profile.aux_edge_bytes / 1024.0,
           giy_profile.aux_ft_slot_bytes / 1024.0,
           giy_profile.aux_jsobject_layout_cache_bytes / 1024.0,
           giy_profile.aux_giysb_staging_bytes / 1024.0,
           giy_profile.aux_giysb_tiny_table_bytes / 1024.0,
           giy_profile.aux_giyol_staging_bytes / 1024.0,
           giy_profile.aux_giyol_batch_bytes / 1024.0,
           giy_profile.aux_padding_bytes / 1024.0);
	    printf("Small memcpy limit:  %zu bytes\n", GIY_NT_COPY_MIN_BYTES);
	    printf("NT copy width:       %d bits\n", GIY_NT_COPY_BITS);
#if USE_GIYSB
    printf("GiYSB enabled:       1\n");
    printf("GiYSB tiny batch:    %d\n", GIYSB_TINY_BATCH);
    printf("GiYSB profile:       %d\n", GIYSB_PROFILE);
    printf("GiYSB staging bytes: %zu\n", (size_t) GIYSB_STAGING_BYTES);
    printf("GiYSB tiny table bytes:%zu\n", (size_t) GIYSB_TINY_TABLE_BYTES);
#if GIYSB_TINY_BATCH
    printf("GiYSB tiny max:      %zu\n", (size_t) GIYSB_TINY_OBJECT_MAX_BYTES);
    printf("GiYSB tiny policy:   staging-only forced NT\n");
#else
    printf("GiYSB small max:     %zu\n", (size_t) GIYSB_SMALL_OBJECT_MAX_BYTES);
    printf("GiYSB tiny policy:   place-only\n");
#endif
    printf("GiYSB old small ratio:%d\n", GIYSB_SMALL_OLD_RATIO);
#if GIYSB_PROFILE
    printf("GiYSB tiny reserved:%llu objs, %.2f MB\n",
           g_giysb_profile.tiny_reserved_objects,
           g_giysb_profile.tiny_reserved_bytes / (1024.0 * 1024.0));
    printf("GiYSB large reserved:%llu objs, %.2f MB\n",
           g_giysb_profile.large_reserved_objects,
           g_giysb_profile.large_reserved_bytes / (1024.0 * 1024.0));
    printf("GiYSB staged tiny:   %llu objs, %.2f MB\n",
           g_giysb_profile.staged_objects,
           g_giysb_profile.staged_bytes / (1024.0 * 1024.0));
    printf("GiYSB max tiny table:%llu entries\n",
           g_giysb_profile.max_tiny_table_entries_per_gc);
    printf("GiYSB staging flushes:%llu (full %llu, tail %llu)\n",
           g_giysb_profile.staging_flushes,
           g_giysb_profile.staging_full_flushes,
           g_giysb_profile.staging_tail_flushes);
#endif
#endif
#if defined(USE_GIYOL)
    printf("GiYOL batch bytes:   %zu\n", (size_t) GIYOL_BATCH_BYTES);
    printf("GiYOL NT batches:    %llu\n", giy_profile.giyol_batches);
    printf("GiYOL batch objects: %llu\n", giy_profile.giyol_batch_objects);
    printf("GiYOL batch bytes NT:%.2f MB\n",
           giy_profile.giyol_batch_bytes / (1024.0 * 1024.0));
    printf("GiYOL small objects: %llu\n",
           giy_profile.giyol_small_flush_objects);
    printf("GiYOL small bytes:   %.2f MB\n",
           giy_profile.giyol_small_flush_bytes / (1024.0 * 1024.0));
#if GIYOL_STAGING_COPY
    printf("GiYOL staging batches:%llu\n", giy_profile.giyol_staging_batches);
    printf("GiYOL staging objects:%llu\n", giy_profile.giyol_staging_objects);
    printf("GiYOL staging bytes: %.2f MB\n",
           giy_profile.giyol_staging_bytes / (1024.0 * 1024.0));
    printf("GiYOL staging fallback objects:%llu\n",
           giy_profile.giyol_staging_fallback_objects);
    printf("GiYOL staging fallback bytes:%.2f MB\n",
           giy_profile.giyol_staging_fallback_bytes / (1024.0 * 1024.0));
#if GIYOL_TINY_STAGING
    printf("GiYOL tiny staging bytes:%zu\n",
           (size_t) GIYOL_TINY_STAGING_BYTES);
    printf("GiYOL tiny flush bytes:%zu\n",
           (size_t) GIYOL_TINY_FLUSH_BYTES);
	    printf("GiYOL tiny max object:%zu\n",
	           (size_t) GIYOL_TINY_MAX_OBJECT_BYTES);
	    printf("GiYOL force tiny tail NT:%d\n", GIYOL_FORCE_TINY_TAIL_NT);
	    printf("GiYOL direct non-tiny NT:%d\n", GIYOL_DIRECT_NONTINY_NT);
	    printf("GiYOL defer tiny staging:%d\n", GIYOL_DEFER_TINY_STAGING);
	    printf("GiYOL tiny flushes:  %llu\n", giy_profile.giyol_tiny_flushes);
	    printf("GiYOL tiny objects:  %llu\n", giy_profile.giyol_tiny_objects);
	    printf("GiYOL tiny bytes:    %.2f MB\n",
	           giy_profile.giyol_tiny_bytes / (1024.0 * 1024.0));
	    printf("GiYOL tiny tail NT flushes:%llu\n",
	           giy_profile.giyol_tiny_tail_nt_flushes);
	    printf("GiYOL tiny tail NT bytes:%.2f MB\n",
	           giy_profile.giyol_tiny_tail_nt_bytes / (1024.0 * 1024.0));
	    printf("GiYOL tiny fallback objects:%llu\n",
	           giy_profile.giyol_tiny_fallback_objects);
	    printf("GiYOL tiny fallback bytes:%.2f MB\n",
	           giy_profile.giyol_tiny_fallback_bytes / (1024.0 * 1024.0));
	    printf("GiYOL direct NT objects:%llu\n",
	           giy_profile.giyol_direct_nt_objects);
	    printf("GiYOL direct NT bytes:%.2f MB\n",
	           giy_profile.giyol_direct_nt_bytes / (1024.0 * 1024.0));
#endif
#endif
#endif
#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
    printf("AS update observations: %llu\n",
           giy_profile.as_update_observations);
    printf("AS update entries:      %llu\n",
           giy_profile.as_update_entries);
    printf("AS update applied:      %llu\n",
           giy_profile.as_update_applied);
    printf("AS update pm changed:   %llu\n",
           giy_profile.as_update_pm_changed);
    printf("AS update invalid shape:%llu\n",
           giy_profile.as_update_invalid_shape);
    printf("AS update invalid pm:   %llu\n",
           giy_profile.as_update_invalid_pm);
    printf("AS update invalid oldpm:%llu\n",
           giy_profile.as_update_invalid_old_pm);
    printf("AS update LUB failed:   %llu\n",
           giy_profile.as_update_lub_failed);
    printf("AS shape advance tries: %llu\n",
           giy_profile.as_shape_advance_attempts);
    printf("AS shape advance chg:   %llu\n",
           giy_profile.as_shape_advance_changed);
#endif
    printf("JSObject precise scan: %d\n", GIY_JSOBJECT_PRECISE_SCAN);
    printf("JSObject precise array: %d\n", GIY_JSOBJECT_PRECISE_ARRAY);
    printf("JSObject shape-only scan: %d\n", GIY_JSOBJECT_SHAPE_ONLY_SCAN);
    printf("JSObject type-aware scan: %d\n", GIY_JSOBJECT_TYPE_AWARE_SCAN);
    printf("JSObject array skip size: %d\n", GIY_JSOBJECT_ARRAY_SKIP_SIZE);
    printf("JSObject scans:       %llu\n", giy_profile.jsobject_scans);
    printf("JSObject precise:     %llu\n",
           giy_profile.jsobject_precise_scans);
    printf("JSObject type-aware:  %llu\n",
           giy_profile.jsobject_type_aware_scans);
    printf("JSObject fallbacks:   %llu\n",
           giy_profile.jsobject_precise_fallbacks);
    printf("JSObject conservative:%llu\n",
           giy_profile.jsobject_conservative_scans);
    printf("JSObject cons slots:  %llu\n",
           giy_profile.jsobject_conservative_slots);
    printf("JSObject precise slots:%llu\n",
           giy_profile.jsobject_precise_slots);
    printf("JSObject type-aware slots:%llu\n",
           giy_profile.jsobject_type_aware_slots);
    printf("JSObject skipped slots:%llu\n",
           giy_profile.jsobject_skipped_slots);
    printf("JSObject shape edges: %llu\n",
           giy_profile.jsobject_shape_edges);
    printf("JSObject young shapes:%llu\n",
           giy_profile.jsobject_shape_layout_young);
    printf("JSObject young PMs:   %llu\n",
           giy_profile.jsobject_pm_layout_young);
    printf("JSObject ext arrays:  %llu\n",
           giy_profile.jsobject_extension_arrays);
    printf("JSObject ext slots:   %llu\n",
           giy_profile.jsobject_extension_slots);
    printf("JSObject layout cache hits:%llu\n",
           giy_profile.jsobject_layout_cache_hits);
    printf("JSObject layout cache misses:%llu\n",
           giy_profile.jsobject_layout_cache_misses);
    printf("JSObject layout cache invalid:%llu\n",
           giy_profile.jsobject_layout_cache_invalid);
    return;
  }

  printf("Minor collections:   %llu\n", giy_profile.minor_collections);
  printf("Young before aux:    %.2f KB\n",
         giy_profile.young_bytes_before_aux / 1024.0);
  printf("Young after aux:     %.2f KB\n",
         giy_profile.young_bytes_after_aux / 1024.0);
  printf("Aux total:           %.2f KB (stack %.2f KB, edge %.2f KB, ft %.2f KB, jsobj layout %.2f KB, giysb staging %.2f KB, giysb tiny table %.2f KB, giyol staging %.2f KB, giyol batch %.2f KB, padding %.2f KB)\n",
         giy_profile.aux_total_bytes / 1024.0,
         giy_profile.aux_stack_bytes / 1024.0,
         giy_profile.aux_edge_bytes / 1024.0,
         giy_profile.aux_ft_slot_bytes / 1024.0,
         giy_profile.aux_jsobject_layout_cache_bytes / 1024.0,
         giy_profile.aux_giysb_staging_bytes / 1024.0,
         giy_profile.aux_giysb_tiny_table_bytes / 1024.0,
         giy_profile.aux_giyol_staging_bytes / 1024.0,
         giy_profile.aux_giyol_batch_bytes / 1024.0,
         giy_profile.aux_padding_bytes / 1024.0);

	  printf("Stack pushes:        %llu\n", giy_profile.stack_pushes);
	  printf("NT copy width:       %d bits\n", GIY_NT_COPY_BITS);
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
#if USE_GIYSB
  printf("GiYSB enabled:       1\n");
  printf("GiYSB tiny batch:    %d\n", GIYSB_TINY_BATCH);
  printf("GiYSB profile:       %d\n", GIYSB_PROFILE);
  printf("GiYSB staging bytes: %zu\n", (size_t) GIYSB_STAGING_BYTES);
  printf("GiYSB tiny table bytes:%zu\n", (size_t) GIYSB_TINY_TABLE_BYTES);
#if GIYSB_TINY_BATCH
  printf("GiYSB tiny max:      %zu\n", (size_t) GIYSB_TINY_OBJECT_MAX_BYTES);
  printf("GiYSB tiny policy:   staging-only forced NT\n");
#else
  printf("GiYSB small max:     %zu\n", (size_t) GIYSB_SMALL_OBJECT_MAX_BYTES);
  printf("GiYSB tiny policy:   place-only\n");
#endif
  printf("GiYSB old small ratio:%d\n", GIYSB_SMALL_OLD_RATIO);
#if GIYSB_PROFILE
  printf("GiYSB tiny reserved: %llu objs, %.2f MB\n",
         g_giysb_profile.tiny_reserved_objects,
         g_giysb_profile.tiny_reserved_bytes / (1024.0 * 1024.0));
  printf("GiYSB tiny overflow: %llu objs, %.2f MB\n",
         g_giysb_profile.tiny_overflow_objects,
         g_giysb_profile.tiny_overflow_bytes / (1024.0 * 1024.0));
  printf("GiYSB large reserved:%llu objs, %.2f MB\n",
         g_giysb_profile.large_reserved_objects,
         g_giysb_profile.large_reserved_bytes / (1024.0 * 1024.0));
  printf("GiYSB staged tiny:   %llu objs, %.2f MB\n",
         g_giysb_profile.staged_objects,
         g_giysb_profile.staged_bytes / (1024.0 * 1024.0));
  printf("GiYSB direct tiny:   %llu objs, %.2f MB\n",
         g_giysb_profile.direct_tiny_objects,
         g_giysb_profile.direct_tiny_bytes / (1024.0 * 1024.0));
  printf("GiYSB max tiny table:%llu entries\n",
         g_giysb_profile.max_tiny_table_entries_per_gc);
  printf("GiYSB staging flushes:%llu (full %llu, tail %llu)\n",
         g_giysb_profile.staging_flushes,
         g_giysb_profile.staging_full_flushes,
         g_giysb_profile.staging_tail_flushes);
#endif
#endif
#if defined(USE_GIYOL)
  printf("GiYOL batch bytes:   %zu\n", (size_t) GIYOL_BATCH_BYTES);
  printf("GiYOL NT batches:    %llu\n", giy_profile.giyol_batches);
  printf("GiYOL batch objects: %llu\n", giy_profile.giyol_batch_objects);
  printf("GiYOL batch bytes NT:%.2f MB\n",
         giy_profile.giyol_batch_bytes / (1024.0 * 1024.0));
  printf("GiYOL small objects: %llu\n",
         giy_profile.giyol_small_flush_objects);
  printf("GiYOL small bytes:   %.2f MB\n",
         giy_profile.giyol_small_flush_bytes / (1024.0 * 1024.0));
#if GIYOL_STAGING_COPY
  printf("GiYOL staging batches:%llu\n", giy_profile.giyol_staging_batches);
  printf("GiYOL staging objects:%llu\n", giy_profile.giyol_staging_objects);
  printf("GiYOL staging bytes: %.2f MB\n",
         giy_profile.giyol_staging_bytes / (1024.0 * 1024.0));
  printf("GiYOL staging fallback objects:%llu\n",
         giy_profile.giyol_staging_fallback_objects);
  printf("GiYOL staging fallback bytes:%.2f MB\n",
         giy_profile.giyol_staging_fallback_bytes / (1024.0 * 1024.0));
#if GIYOL_TINY_STAGING
  printf("GiYOL tiny staging bytes:%zu\n",
         (size_t) GIYOL_TINY_STAGING_BYTES);
  printf("GiYOL tiny flush bytes:%zu\n",
         (size_t) GIYOL_TINY_FLUSH_BYTES);
	  printf("GiYOL tiny max object:%zu\n",
	         (size_t) GIYOL_TINY_MAX_OBJECT_BYTES);
	  printf("GiYOL force tiny tail NT:%d\n", GIYOL_FORCE_TINY_TAIL_NT);
	  printf("GiYOL direct non-tiny NT:%d\n", GIYOL_DIRECT_NONTINY_NT);
	  printf("GiYOL defer tiny staging:%d\n", GIYOL_DEFER_TINY_STAGING);
	  printf("GiYOL tiny flushes:  %llu\n", giy_profile.giyol_tiny_flushes);
	  printf("GiYOL tiny objects:  %llu\n", giy_profile.giyol_tiny_objects);
	  printf("GiYOL tiny bytes:    %.2f MB\n",
	         giy_profile.giyol_tiny_bytes / (1024.0 * 1024.0));
	  printf("GiYOL tiny tail NT flushes:%llu\n",
	         giy_profile.giyol_tiny_tail_nt_flushes);
	  printf("GiYOL tiny tail NT bytes:%.2f MB\n",
	         giy_profile.giyol_tiny_tail_nt_bytes / (1024.0 * 1024.0));
	  printf("GiYOL tiny fallback objects:%llu\n",
	         giy_profile.giyol_tiny_fallback_objects);
	  printf("GiYOL tiny fallback bytes:%.2f MB\n",
	         giy_profile.giyol_tiny_fallback_bytes / (1024.0 * 1024.0));
	  printf("GiYOL direct NT objects:%llu\n",
	         giy_profile.giyol_direct_nt_objects);
	  printf("GiYOL direct NT bytes:%.2f MB\n",
	         giy_profile.giyol_direct_nt_bytes / (1024.0 * 1024.0));
#endif
#endif
#endif
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
  printf("JSObject precise scan: %d\n", GIY_JSOBJECT_PRECISE_SCAN);
  printf("JSObject precise array: %d\n", GIY_JSOBJECT_PRECISE_ARRAY);
  printf("JSObject shape-only scan: %d\n", GIY_JSOBJECT_SHAPE_ONLY_SCAN);
  printf("JSObject type-aware scan: %d\n", GIY_JSOBJECT_TYPE_AWARE_SCAN);
  printf("JSObject array skip size: %d\n", GIY_JSOBJECT_ARRAY_SKIP_SIZE);
  printf("JSObject scans:       %llu\n", giy_profile.jsobject_scans);
  printf("JSObject precise:     %llu\n",
         giy_profile.jsobject_precise_scans);
  printf("JSObject type-aware:  %llu\n",
         giy_profile.jsobject_type_aware_scans);
  printf("JSObject fallbacks:   %llu\n",
         giy_profile.jsobject_precise_fallbacks);
  printf("JSObject conservative:%llu\n",
         giy_profile.jsobject_conservative_scans);
  printf("JSObject cons slots:  %llu\n",
         giy_profile.jsobject_conservative_slots);
  printf("JSObject precise slots:%llu\n",
         giy_profile.jsobject_precise_slots);
  printf("JSObject type-aware slots:%llu\n",
         giy_profile.jsobject_type_aware_slots);
  printf("JSObject skipped slots:%llu\n",
         giy_profile.jsobject_skipped_slots);
  printf("JSObject shape edges: %llu\n",
         giy_profile.jsobject_shape_edges);
  printf("JSObject young shapes:%llu\n",
         giy_profile.jsobject_shape_layout_young);
  printf("JSObject young PMs:   %llu\n",
         giy_profile.jsobject_pm_layout_young);
  printf("JSObject ext arrays:  %llu\n",
         giy_profile.jsobject_extension_arrays);
  printf("JSObject ext slots:   %llu\n",
         giy_profile.jsobject_extension_slots);
  printf("JSObject layout cache hits:%llu\n",
         giy_profile.jsobject_layout_cache_hits);
  printf("JSObject layout cache misses:%llu\n",
         giy_profile.jsobject_layout_cache_misses);
  printf("JSObject layout cache invalid:%llu\n",
         giy_profile.jsobject_layout_cache_invalid);
#if GIY_AS_DRY_PROFILE && defined(ALLOC_SITE_CACHE)
  printf("AS dry objects:       %llu\n", giy_profile.as_dry_objects);
  printf("AS dry pm null:       %llu\n", giy_profile.as_dry_pm_null);
  printf("AS dry pm match:      %llu\n", giy_profile.as_dry_pm_match);
  printf("AS dry pm mismatch:   %llu\n", giy_profile.as_dry_pm_mismatch);
#endif
#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
  printf("AS update observations: %llu\n",
         giy_profile.as_update_observations);
  printf("AS update entries:      %llu\n",
         giy_profile.as_update_entries);
  printf("AS update applied:      %llu\n",
         giy_profile.as_update_applied);
  printf("AS update pm changed:   %llu\n",
         giy_profile.as_update_pm_changed);
  printf("AS update invalid shape:%llu\n",
         giy_profile.as_update_invalid_shape);
  printf("AS update invalid pm:   %llu\n",
         giy_profile.as_update_invalid_pm);
  printf("AS update invalid oldpm:%llu\n",
         giy_profile.as_update_invalid_old_pm);
  printf("AS update LUB failed:   %llu\n",
         giy_profile.as_update_lub_failed);
  printf("AS shape advance tries: %llu\n",
         giy_profile.as_shape_advance_attempts);
  printf("AS shape advance chg:   %llu\n",
         giy_profile.as_shape_advance_changed);
#endif

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
#if USE_GIYSB && GIYSB_TINY_BATCH
  giysb_staging_reset();
  giysb_tiny_table_reset();
#endif
	  giy_jsobject_layout_cache_begin_minor_gc();
#if defined(USE_GIYOL) && GIYOL_STAGING_COPY
  giyol_reserve_order_batch_begin();
#endif
  giy_as_update_log_reset();
#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
  giy_as_object_update_log_reset();
#endif
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
  giy_old_guard_set_phase("allocation_site_update_old_metadata");
  giy_old_guard_end();
#if GIY_AS_UPDATE && defined(ALLOC_SITE_CACHE)
  giy_apply_materialized_object_alloc_site_updates();
#endif
  giy_apply_alloc_site_updates();
  giy_advance_function_table_alloc_sites(ctx);
  giy_old_guard_begin();

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
