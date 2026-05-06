#include <exception>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#ifdef __linux__
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#endif
#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "log.h"

Context *the_context;
#include "gc-visitor-inl.h"
#include "cache_dram_manager.h"


Cache_space cache_space;
Dram_space dram_space;

int initial_alloc_bytes;
int minor_gc_count = 0;
long pass_the_remember_set_count = 0;
long write_barrier_calls = 0;
long write_barrier_duplicate_filtered = 0;
int in_minor_gc = 0;

// Performance profiling: GC breakdown
long long total_scan_roots_time = 0;
long long total_scan_rs_time = 0;
long long total_scavenge_time = 0;

// Performance profiling: allocation and forward ops (avoid conflict with GC_PROF)
long long generational_alloc_count = 0;
long long generational_alloc_bytes = 0;
long long generational_forward_count = 0;

#ifndef ALLOC_SIZE_PROFILE
#define ALLOC_SIZE_PROFILE 0
#endif

#ifndef GIY_WB_PROFILE
#define GIY_WB_PROFILE 0
#endif

#ifndef CACHE_SIZE_KB
#define CACHE_SIZE_KB 512
#endif

#if GIY_WB_PROFILE
extern "C" void giy_print_wb_profile();
#endif

#if ALLOC_SIZE_PROFILE
static const size_t alloc_size_bucket_limits[] = {
    8, 16, 24, 32, 40, 48, 56, 64,
    80, 96, 112, 128, 160, 192, 224, 256,
    320, 384, 512, 768, 1024, 1536, 2048, 4096,
    8192, 16384, 32768, 65536
};
static const int alloc_size_bucket_count =
    (int)(sizeof(alloc_size_bucket_limits) / sizeof(alloc_size_bucket_limits[0])) + 1;

static unsigned long long alloc_payload_count[sizeof(alloc_size_bucket_limits) / sizeof(alloc_size_bucket_limits[0]) + 1];
static unsigned long long alloc_payload_bytes[sizeof(alloc_size_bucket_limits) / sizeof(alloc_size_bucket_limits[0]) + 1];
static unsigned long long alloc_footprint_count[sizeof(alloc_size_bucket_limits) / sizeof(alloc_size_bucket_limits[0]) + 1];
static unsigned long long alloc_footprint_bytes[sizeof(alloc_size_bucket_limits) / sizeof(alloc_size_bucket_limits[0]) + 1];
static unsigned long long alloc_count_by_type[256];
static unsigned long long alloc_payload_bytes_by_type[256];
static unsigned long long alloc_footprint_bytes_by_type[256];
static unsigned long long alloc_profile_total_count;
static unsigned long long alloc_profile_payload_bytes;
static unsigned long long alloc_profile_footprint_bytes;

static int alloc_size_bucket_index(size_t bytes)
{
    int limit_count = alloc_size_bucket_count - 1;
    for (int i = 0; i < limit_count; i++) {
        if (bytes <= alloc_size_bucket_limits[i])
            return i;
    }
    return limit_count;
}

static void alloc_size_profile_note(size_t payload_bytes,
                                    size_t footprint_bytes,
                                    cell_type_t type)
{
    int payload_bucket = alloc_size_bucket_index(payload_bytes);
    int footprint_bucket = alloc_size_bucket_index(footprint_bytes);
    unsigned int t = (unsigned int) type;

    alloc_payload_count[payload_bucket]++;
    alloc_payload_bytes[payload_bucket] += payload_bytes;
    alloc_footprint_count[footprint_bucket]++;
    alloc_footprint_bytes[footprint_bucket] += footprint_bytes;

    if (t < 256) {
        alloc_count_by_type[t]++;
        alloc_payload_bytes_by_type[t] += payload_bytes;
        alloc_footprint_bytes_by_type[t] += footprint_bytes;
    }

    alloc_profile_total_count++;
    alloc_profile_payload_bytes += payload_bytes;
    alloc_profile_footprint_bytes += footprint_bytes;
}

static void alloc_size_profile_print_bucket(const char *label,
                                            int index,
                                            unsigned long long count,
                                            unsigned long long bytes,
                                            unsigned long long total_count,
                                            unsigned long long total_bytes)
{
    if (count == 0)
        return;

    int limit_count = alloc_size_bucket_count - 1;
    if (index == 0) {
        printf("%-20s <=%-6zu %14llu %8.2f%% %12.2f MB %8.2f%%\n",
               label, alloc_size_bucket_limits[index], count,
               total_count ? 100.0 * count / total_count : 0.0,
               bytes / (1024.0 * 1024.0),
               total_bytes ? 100.0 * bytes / total_bytes : 0.0);
    } else if (index < limit_count) {
        printf("%-20s %5zu-%-6zu %14llu %8.2f%% %12.2f MB %8.2f%%\n",
               label, alloc_size_bucket_limits[index - 1] + 1,
               alloc_size_bucket_limits[index], count,
               total_count ? 100.0 * count / total_count : 0.0,
               bytes / (1024.0 * 1024.0),
               total_bytes ? 100.0 * bytes / total_bytes : 0.0);
    } else {
        printf("%-20s >%-6zu %14llu %8.2f%% %12.2f MB %8.2f%%\n",
               label, alloc_size_bucket_limits[limit_count - 1], count,
               total_count ? 100.0 * count / total_count : 0.0,
               bytes / (1024.0 * 1024.0),
               total_bytes ? 100.0 * bytes / total_bytes : 0.0);
    }
}

static void alloc_size_profile_print()
{
    printf("\n=== Allocation Size Distribution ===\n");
    printf("note: payload is request_bytes; footprint is ALIGN(payload + object_header)\n");
    printf("Total profiled allocs: %llu\n", alloc_profile_total_count);
    printf("Total payload bytes:   %.2f MB\n",
           alloc_profile_payload_bytes / (1024.0 * 1024.0));
    printf("Total footprint bytes: %.2f MB\n",
           alloc_profile_footprint_bytes / (1024.0 * 1024.0));
    if (alloc_profile_total_count > 0) {
        printf("Avg payload size:     %.1f bytes\n",
               (double) alloc_profile_payload_bytes / alloc_profile_total_count);
        printf("Avg footprint size:   %.1f bytes\n",
               (double) alloc_profile_footprint_bytes / alloc_profile_total_count);
    }

    printf("\nPayload histogram:\n");
    printf("%-20s %-12s %14s %9s %12s %9s\n",
           "kind", "bytes", "count", "count%", "payload", "bytes%");
    for (int i = 0; i < alloc_size_bucket_count; i++) {
        alloc_size_profile_print_bucket("payload", i,
                                        alloc_payload_count[i],
                                        alloc_payload_bytes[i],
                                        alloc_profile_total_count,
                                        alloc_profile_payload_bytes);
    }

    printf("\nFootprint histogram:\n");
    printf("%-20s %-12s %14s %9s %12s %9s\n",
           "kind", "bytes", "count", "count%", "footprint", "bytes%");
    for (int i = 0; i < alloc_size_bucket_count; i++) {
        alloc_size_profile_print_bucket("footprint", i,
                                        alloc_footprint_count[i],
                                        alloc_footprint_bytes[i],
                                        alloc_profile_total_count,
                                        alloc_profile_footprint_bytes);
    }

    printf("\nAllocation by cell type:\n");
    printf("%-8s %14s %12s %12s %12s\n",
           "type", "count", "count%", "payload MB", "footprint MB");
    for (int i = 0; i < 256; i++) {
        if (alloc_count_by_type[i] == 0)
            continue;
        printf("0x%02x     %14llu %11.2f%% %12.2f %12.2f\n",
               i, alloc_count_by_type[i],
               alloc_profile_total_count ?
                   100.0 * alloc_count_by_type[i] / alloc_profile_total_count : 0.0,
               alloc_payload_bytes_by_type[i] / (1024.0 * 1024.0),
               alloc_footprint_bytes_by_type[i] / (1024.0 * 1024.0));
    }
}
#endif

// Performance profiling: overall timing
long long total_gc_full_wall_time = 0;
long long total_gc_full_cpu_time = 0;
struct timespec program_start_wall_time;
struct timespec program_end_wall_time;
struct timespec program_start_cpu_time;
struct timespec program_end_cpu_time;

// PMU counters for cache-miss observation only during GC windows.
int gc_pmu_initialized = 0;
int gc_pmu_supported = 0;
int gc_pmu_fd_cache_misses = -1;
int gc_pmu_fd_cache_references = -1;
int gc_pmu_fd_instructions = -1;
long long total_gc_cache_misses = 0;
long long total_gc_cache_references = 0;
long long total_gc_instructions = 0;
int gc_pmu_sampled_gc_count = 0;
int gc_pmu_init_errno = 0;
int gc_pmu_perf_event_paranoid = -999;

static inline int gc_perf_event_open(struct perf_event_attr *attr, pid_t pid,
                                     int cpu, int group_fd, unsigned long flags)
{
#ifdef __linux__
    return (int) syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
#else
    (void) attr; (void) pid; (void) cpu; (void) group_fd; (void) flags;
    return -1;
#endif
}

static int gc_open_hw_counter(unsigned long long config)
{
#ifdef __linux__
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HARDWARE;
    attr.size = sizeof(attr);
    attr.config = config;
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    return gc_perf_event_open(&attr, 0, -1, -1, 0);
#else
    (void) config;
    return -1;
#endif
}

static int gc_open_hw_cache_counter(int cache_id, int op_id, int result_id)
{
#ifdef __linux__
    struct perf_event_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HW_CACHE;
    attr.size = sizeof(attr);
    attr.config = (unsigned long long) cache_id |
                  ((unsigned long long) op_id << 8) |
                  ((unsigned long long) result_id << 16);
    attr.disabled = 1;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    return gc_perf_event_open(&attr, 0, -1, -1, 0);
#else
    (void) cache_id; (void) op_id; (void) result_id;
    return -1;
#endif
}

static int gc_read_perf_event_paranoid(void)
{
#ifdef __linux__
    FILE *fp = fopen("/proc/sys/kernel/perf_event_paranoid", "r");
    int value = -999;
    if (!fp)
        return value;
    if (fscanf(fp, "%d", &value) != 1)
        value = -999;
    fclose(fp);
    return value;
#else
    return -999;
#endif
}

static void gc_pmu_init_once(void)
{
    if (gc_pmu_initialized)
        return;
    gc_pmu_initialized = 1;

#ifdef __linux__
    int saved_errno = 0;
    gc_pmu_perf_event_paranoid = gc_read_perf_event_paranoid();

    gc_pmu_fd_cache_misses = gc_open_hw_counter(PERF_COUNT_HW_CACHE_MISSES);
    if (gc_pmu_fd_cache_misses < 0) {
        if (errno != 0)
            saved_errno = errno;
        gc_pmu_fd_cache_misses = gc_open_hw_cache_counter(
            PERF_COUNT_HW_CACHE_LL,
            PERF_COUNT_HW_CACHE_OP_READ,
            PERF_COUNT_HW_CACHE_RESULT_MISS);
    }

    gc_pmu_fd_cache_references = gc_open_hw_counter(PERF_COUNT_HW_CACHE_REFERENCES);
    if (gc_pmu_fd_cache_references < 0) {
        if (errno != 0)
            saved_errno = errno;
        gc_pmu_fd_cache_references = gc_open_hw_cache_counter(
            PERF_COUNT_HW_CACHE_LL,
            PERF_COUNT_HW_CACHE_OP_READ,
            PERF_COUNT_HW_CACHE_RESULT_ACCESS);
    }

    gc_pmu_fd_instructions = gc_open_hw_counter(PERF_COUNT_HW_INSTRUCTIONS);
    if (gc_pmu_fd_instructions < 0 && errno != 0)
        saved_errno = errno;

    gc_pmu_supported = (gc_pmu_fd_cache_misses >= 0 ||
                        gc_pmu_fd_cache_references >= 0 ||
                        gc_pmu_fd_instructions >= 0);
    if (!gc_pmu_supported)
        gc_pmu_init_errno = saved_errno;
#else
    gc_pmu_supported = 0;
#endif
}

static inline void gc_pmu_counter_start(int fd)
{
#ifdef __linux__
    if (fd < 0)
        return;
    ioctl(fd, PERF_EVENT_IOC_RESET, 0);
    ioctl(fd, PERF_EVENT_IOC_ENABLE, 0);
#else
    (void) fd;
#endif
}

static inline long long gc_pmu_counter_stop_and_read(int fd)
{
#ifdef __linux__
    long long value = 0;
    if (fd < 0)
        return 0;
    ioctl(fd, PERF_EVENT_IOC_DISABLE, 0);
    if (read(fd, &value, sizeof(value)) != (ssize_t) sizeof(value))
        return 0;
    return value;
#else
    (void) fd;
    return 0;
#endif
}

static void gc_pmu_start_window(void)
{
    if (!gc_pmu_supported)
        return;
    gc_pmu_counter_start(gc_pmu_fd_cache_misses);
    gc_pmu_counter_start(gc_pmu_fd_cache_references);
    gc_pmu_counter_start(gc_pmu_fd_instructions);
}

static void gc_pmu_stop_window(void)
{
    if (!gc_pmu_supported)
        return;
    total_gc_cache_misses += gc_pmu_counter_stop_and_read(gc_pmu_fd_cache_misses);
    total_gc_cache_references += gc_pmu_counter_stop_and_read(gc_pmu_fd_cache_references);
    total_gc_instructions += gc_pmu_counter_stop_and_read(gc_pmu_fd_instructions);
    gc_pmu_sampled_gc_count++;
}

static inline long long timespec_diff_ns(const struct timespec &start, const struct timespec &end)
{
    return (end.tv_sec - start.tv_sec) * 1000000000LL +
           (end.tv_nsec - start.tv_nsec);
}

void scavenge();
void scan_init_area();
void scan_remembered_set();
void garbage_collection(Context *ctx);
static void print_gc_status();
static long scan_init_objects_count = 0;

inline bool need_major_gc()
{
    if (cache_space.end - cache_space.work_begin >= dram_space.available_bytes)
        return true;
    return false;
}

void print_dram_space_usage()
{
    int used_bytes = dram_space.total_size - dram_space.available_bytes;
    if(dram_space.available_bytes!= dram_space.end - dram_space.free){
        printf("Warning: dram_space.available_bytes inconsistent with pointer!!!\n");
        exit(1);
    }
    // printf("DRAM space usage: used %d KB, available bytes: %d KB, total %d KB\n", used_bytes / 1024, dram_space.available_bytes / 1024, dram_space.total_size / 1024);
}

void print_cache_space_useage()
{
    int used_bytes = cache_space.current - cache_space.work_begin;
    // printf("Cache space usage: used %f KB\n", used_bytes / 1024.0);
}

class GiYIsolatedTracer
{
private:

    // Inline and optimize cache space check - called billions of times!
    static inline bool in_cache_space(uintptr_t ptr) {
        // Use unsigned arithmetic to optimize range check into single comparison
        // This converts two comparisons into one subtraction + comparison
        return (ptr - cache_space.work_begin) < (cache_space.end - cache_space.work_begin);
    }

    static inline bool in_dram_space(uintptr_t ptr) {
        return (ptr - dram_space.begin) < dram_space.total_size;
    }

    //ptr include header and payload, size don't include header
    static void copy_to_dram(uintptr_t ptr, int size) {
        if (dram_space.available_bytes < size + sizeof(object_header)) {
            printf("DRAM space full, cannot copy object of size %lu bytes\n", size);
            exit(1);
        }
        object_header *new_obj_hdr = (object_header *) dram_space.free;

        int align_bytes = ALIGN(size + sizeof(object_header));

        memcpy((void*)new_obj_hdr, (void*)ptr , align_bytes);
        
        dram_space.free += align_bytes;
        dram_space.available_bytes -= align_bytes;

        // set forwarding pointer in cache space object header
        ((object_header *)ptr)->forwarding_pointer = (uintptr_t)(new_obj_hdr + 1);

        // printf("Copied object of size %lu bytes from cache %p to dram %p\n", size, (void*)ptr, (void*)new_payload_ptr);
    }

public:

    static constexpr bool is_single_object_scanner = false;
    static constexpr bool is_hcg_mutator = false;


    //ptr point to payload
    static uintptr_t forward(uintptr_t ptr) {
        generational_forward_count++;  // Count forwarding operations
        
        // if (ptr == 0)
        // {
        //     return 0;
        // }
        if (in_dram_space(ptr)) {
            return ptr;
        }

        // rememberset_add(ptr);

        if(ptr < cache_space.work_begin && ptr >= cache_space.begin){
            return ptr;
        }

        object_header *obj_hdr = (object_header *)ptr - 1;
        if (obj_hdr->forwarding_pointer)
            return obj_hdr->forwarding_pointer;
        // void *payload = (void *) ptr;
        int size = obj_hdr->size;
        // cell_type_t type = obj_hdr->type;
        copy_to_dram((uintptr_t)obj_hdr, size);

        return obj_hdr->forwarding_pointer;
    }


    //this function move the object from cache space to dram space, but just move, not update the references
    static void process_edge(JSValue &v) {
        // Fast path: check if it's an immediate value first (most common in registers)
        if (is_fixnum(v) || is_special(v))
            return;

        uintptr_t ptr = (uintptr_t) clear_ptag(v);
        
        // Fast path: for generational GC, most pointers (especially in function table)
        // are in old generation (DRAM). Check this FIRST before cache check.
        # ifdef USE_REMEMBERED_SET
        // Single range check - if in DRAM, nothing to do
        if (in_dram_space(ptr))
            return;
        // If not in cache work area (might be in init area or invalid), skip
        if (!in_cache_space(ptr))
            return;
        # endif

        pass_the_remember_set_count++;
        uintptr_t to = forward(ptr);
        PTag tag = get_ptag(v);
        v = put_ptag(to, tag);
    }
    static void process_edge(void *&p) {
        uintptr_t ptr = (uintptr_t) p;

        // Fast path: most void* pointers in function table point to DRAM
        # ifdef USE_REMEMBERED_SET
        if (in_dram_space(ptr))
            return;
        if (!in_cache_space(ptr))
            return;
        # endif
        pass_the_remember_set_count++;

        uintptr_t to = forward(ptr);
        p = (void *) to;
    }

    static void process_edge_function_frame(JSValue &v) {
        void *p = jsv_to_function_frame(v);
        uintptr_t ptr = (uintptr_t) p;
        
        //use remember set
        # ifdef USE_REMEMBERED_SET
        if (!in_cache_space(ptr))
            return;
        # endif
                pass_the_remember_set_count++;
        if (ptr == 0)
            return;
        
        
        // assert(in_cache_space(ptr));
        
        uintptr_t new_ptr = forward(ptr);
        v = (JSValue) (uintjsv_t) new_ptr;
    }

    static void process_edge_ex_JSValue_array(JSValue *&array, size_t size) {
        if (array == NULL)
            return;
        uintptr_t ptr = (uintptr_t) array;
        
        //use remember set
        # ifdef USE_REMEMBERED_SET
        if (!in_cache_space(ptr))
            return;
        # endif
        pass_the_remember_set_count++;
        uintptr_t new_ptr = forward(ptr);


        array = (JSValue *) new_ptr;
        // JSValue *new_array = (JSValue *) new_ptr;
        // for (size_t i = 0; i < size; i++) {
        //     process_edge(new_array[i]);
        // }
    }

    static void process_edge_ex_JSValue_array(JSValue &array_ref, size_t size) {
        JSValue *array = (JSValue *) clear_ptag(array_ref);
        if (array == NULL)
            return;
        uintptr_t ptr = (uintptr_t) array;
        
        //use remember set
        # ifdef USE_REMEMBERED_SET
        if (!in_cache_space(ptr))
            return;
        # endif

        pass_the_remember_set_count++;
        uintptr_t new_ptr = forward(ptr);
        PTag tag = get_ptag(array_ref);
        array_ref = put_ptag(new_ptr, tag);
        // JSValue *new_array = (JSValue *) new_ptr;
        // for (size_t i = 0; i < size; i++) {
        //     process_edge(new_array[i]);
        // }
    }

    static void process_node_JSValue_array(JSValue *p) {
        // if (p == NULL)
        //     return;
        // size_t length = (size_t) number_to_double(p[0]);
        // for (size_t i = 1; i <= length; i++) {
        //     process_edge(p[i]);
        // }
        // object_header *hdrp = ((object_header *) p) - 1;
        // size_t payload_granules = hdrp->size;
        // size_t slots = payload_granules <<
        // (LOG_BYTES_IN_GRANULE - LOG_BYTES_IN_JSVALUE);
        // for (size_t i = 0; i < slots; i++)
        // process_edge(p[i]);




        object_header *hdrp = ((object_header *) p) - 1;
        size_t payload_bytes = hdrp->size;
        size_t slots = payload_bytes / sizeof(JSValue);  
        for (size_t i = 0; i < slots; i++)
            process_edge(p[i]);
    }

    static void process_mark_stack() {}

    static void process_edge_ex_ptr_array(void **&array, size_t size) {
        if (array == NULL)
            return;
        uintptr_t ptr = (uintptr_t) array;
        //use remember set
        # ifdef USE_REMEMBERED_SET
        if (!in_cache_space(ptr))
            return;
        # endif
        pass_the_remember_set_count++;
        uintptr_t new_ptr = forward(ptr);
        array = (void **) new_ptr;
        // void **new_array = (void **) new_ptr;
        // for (size_t i = 0; i < size; i++) {
        //     if(new_array[i]!= NULL)
        //         process_edge(new_array[i]);
        // }
    }

    static void process_weak_edge(JSValue &v) {
        process_edge(v);
    }
    static void process_weak_edge(void *&p) {
        process_edge(p);
    }



    //What should I do here?
    static bool is_marked_cell(void *p) {
        if (p == NULL)
            return false;
        
        uintptr_t ptr = (uintptr_t) p;
        
        if (in_dram_space(ptr))
            return true;
        
        if (cache_space.begin <= ptr && ptr < cache_space.work_begin)
            return true;
        
        if (in_cache_space(ptr)) {
            object_header *hdrp = (object_header *) ptr - 1;
            return hdrp->forwarding_pointer != 0;
        }
        
        return false;

    }

    GiYIsolatedTracer(/* args */);
    ~GiYIsolatedTracer
();
};

GiYIsolatedTracer::GiYIsolatedTracer(/* args */)
{
}

GiYIsolatedTracer::~GiYIsolatedTracer()
{
}

// Optimized root scanning for generational GC: skip function table to avoid 
// scanning thousands of inline caches that mostly point to old generation
static void scan_roots_generational(Context *ctx) {
    // Scan global variables
    {
        struct global_constant_objects *gconstsp = &gconsts;
        JSValue *p;
        for (p = (JSValue *) gconstsp; p < (JSValue *) (gconstsp + 1); p++)
            GiYIsolatedTracer::process_edge(*p);
    }
    {
        struct global_property_maps *gpmsp = &gpms;
        PropertyMap **p;
        for (p = (PropertyMap **) gpmsp; p < (PropertyMap **) (gpmsp + 1); p++) {
            void *ptr = (void*)*p;
            GiYIsolatedTracer::process_edge(ptr);
            *p = (PropertyMap*)ptr;
        }
    }
    {
        struct global_object_shapes *gshapesp = &gshapes;
        Shape** p;
        for (p = (Shape **) gshapesp; p < (Shape **) (gshapesp + 1); p++) {
            void *ptr = (void*)*p;
            GiYIsolatedTracer::process_edge(ptr);
            *p = (Shape*)ptr;
        }
    }

    // Scan Context (global, special registers, stack, exception handlers)
    GiYIsolatedTracer::process_edge(ctx->global);
    // SKIP function table scanning - it's treated as old generation!
    {
        void *ptr = (void*)ctx->spreg.lp;
        GiYIsolatedTracer::process_edge(ptr);
        ctx->spreg.lp = (FunctionFrame*)ptr;
    }
    GiYIsolatedTracer::process_edge(ctx->spreg.a);
    GiYIsolatedTracer::process_edge(ctx->spreg.err);
    if (ctx->exhandler_stack_top != NULL) {
        void *ptr = (void*)ctx->exhandler_stack_top;
        GiYIsolatedTracer::process_edge(ptr);
        ctx->exhandler_stack_top = (UnwindProtect*)ptr;
    }
    GiYIsolatedTracer::process_edge(ctx->lcall_stack);

    // Scan stack
    JSValue* stack = ctx->stack;
    int sp = ctx->spreg.sp;
    int fp = ctx->spreg.fp;
    while (1) {
        while (sp >= fp) {
            GiYIsolatedTracer::process_edge(stack[sp]);
            sp--;
        }
        if (sp < 0)
            break;
        fp = stack[sp--]; /* FP */
        GiYIsolatedTracer::process_edge_function_frame(stack[sp--]); /* LP */
        sp--; /* PC */
        sp--; /* CF */
    }

    // Scan GC_PUSH'ed roots
    for (int i = 0; i < gc_root_stack_ptr; i++)
        GiYIsolatedTracer::process_edge(*(gc_root_stack[i]));
}


void space_init(size_t bytes, size_t threshold_bytes)
{
    atexit(print_gc_status);

    printf("object_header size: %lu bytes\n", sizeof(object_header));
    initial_alloc_bytes = 0;
    //initial cache space
    long cache_size = CACHE_SIZE_KB * 1024; // cache space
    // long cache_size = 100*1024*1024; // 100MB cache space
    cache_space.begin = (uintptr_t) malloc(cache_size); // 512KB cache space
    cache_space.current = cache_space.begin;
    cache_space.total_size = cache_size;
    cache_space.threshold_size = threshold_bytes;

    //test
    // cache_space.threshold_size = 1024; // 1KB threshold for testing
    // cache_space.threshold_size = cache_size / 10;
    cache_space.threshold_size = 512;


    cache_space.end = cache_space.begin + cache_space.total_size;

    init_remembered_set();



   

    // printf("Cache space allocated at address %p, to %p\n", (void*)cache_space.begin, (void*)(cache_space.begin + cache_space.total_size));
    printf("init info: cache_space.begin=%p, cache_space.end=%p, total_size=%dKB, threshold_size=%dMB\n",
           (void*)cache_space.begin, (void*)cache_space.end, cache_space.total_size/1024, cache_space.threshold_size/1024/1024);


    // Keep cache size unchanged; allocate the largest feasible DRAM region.
    const size_t dram_candidates[] = {
        (size_t)128ULL * 1024ULL * 1024ULL * 1024ULL, // 128GB
        (size_t)64ULL  * 1024ULL * 1024ULL * 1024ULL, // 64GB
        (size_t)32ULL  * 1024ULL * 1024ULL * 1024ULL, // 32GB
        (size_t)16ULL  * 1024ULL * 1024ULL * 1024ULL  // 16GB
    };
    dram_space.begin = 0;
    for (size_t i = 0; i < sizeof(dram_candidates) / sizeof(dram_candidates[0]); i++) {
        bytes = dram_candidates[i];
        dram_space.begin = (uintptr_t) malloc(bytes);
        if (dram_space.begin != 0)
            break;
    }
    if (dram_space.begin == 0) {
        perror("malloc(dram_space)");
        exit(1);
    }
    dram_space.free = dram_space.begin;
    dram_space.current = dram_space.begin;
    dram_space.total_size = bytes;
    dram_space.available_bytes = bytes;
    dram_space.end = dram_space.begin + dram_space.total_size;

#ifdef USE_GIYOL
        printf("Now we are using GiYOL GC. Cache DRAM Manager initialized: Cache size %d Kbytes, DRAM size %zu Kbytes\n",
            cache_space.total_size / 1024, dram_space.total_size / 1024);
#else
        printf("Now we are using GiY GC. Cache DRAM Manager initialized: Cache size %d Kbytes, DRAM size %zu Kbytes\n",
            cache_space.total_size / 1024, dram_space.total_size / 1024);
#endif
        printf("DRAM address info: begin=%p, end=%p, total_size=%zuKB\n",
            (void*)dram_space.begin, (void*)dram_space.end, dram_space.total_size/1024);

    print_dram_space_usage();
}

void *space_alloc(uintptr_t request_bytes, cell_type_t type)
{
    // Count allocations for performance analysis
    generational_alloc_count++;
    generational_alloc_bytes += request_bytes;
    
    // printf("Cache DRAM Manager allocating %lu bytes, type is %#x\n", request_bytes, type);
    // printf("allocate at address %p\n", (void*) (cache_space.current + sizeof(object_header)));


    if (request_bytes == 0) {
        assert(type == CELLT_ARRAY_DATA);
        JSValue *array = (JSValue *) space_alloc(sizeof(JSValue), type);
        array[0] = JS_UNDEFINED;
        return array;
    }

    // printf("Allocating %lu bytes in cache space for type 0x%02x\n", request_bytes, type);

    int total_byte = (int)request_bytes + sizeof(object_header);
    int align_bytes = ALIGN(total_byte);
    #if ALLOC_SIZE_PROFILE
    alloc_size_profile_note((size_t) request_bytes, (size_t) align_bytes, type);
    #endif
    if(cache_space.current + align_bytes > cache_space.end) {
        printf("Cache space full, need to trigger GC!!!\n");
        // exit(1);
        garbage_collection(the_context);
        if(cache_space.current + align_bytes > cache_space.end) {
            printf("Error: Not enough space even after GC for allocation of %lu bytes\n", request_bytes);
            exit(1);
        }
    }

    object_header *obj_hdr = (object_header *) (cache_space.current);
    obj_hdr->type = type;
    obj_hdr->size = (long int)request_bytes;
    obj_hdr->forwarding_pointer = 0; //initially no forwarding pointer
    cache_space.current += align_bytes;

    void *obj_payload = (void*)(obj_hdr + 1);
    // memset(obj_payload, 0, request_bytes); //zero initialize the allocated memory


    initial_alloc_bytes+= align_bytes;
    // if(init_finish)
    //     printf("this time use %d bytes, currently allocated total %d bytes in cache space\n", align_bytes, initial_alloc_bytes);


    return obj_payload;
}



void garbage_collection(Context *ctx)
{
  /* initialise */
    the_context = ctx;
    
    // dram_space.current = dram_space.begin;
    if(need_major_gc()) {
        printf("===============major GC triggered=================\n");
        //todo: implement major GC
        exit(1);
    }

    struct timespec gc_wall_start, gc_wall_end;
    struct timespec gc_cpu_start, gc_cpu_end;
    clock_gettime(CLOCK_MONOTONIC, &gc_wall_start);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &gc_cpu_start);
    gc_pmu_start_window();

    print_cache_space_useage();
    // printf("=================minor GC triggered==================\n");
    minor_gc_count++;
    in_minor_gc = 1;
    // printf("Minor GC count: %d, size of rememberset %d\n", minor_gc_count, remembered_set.count);

    // GiY-style minor path: resolve pointer graph in young first,
    // then materialize copied objects in DRAM.
    long long scan_roots_ns = 0;
    long long scan_rs_ns = 0;
    long long young_trace_ns = 0;
    giy_minor_collect(ctx, &scan_roots_ns, &scan_rs_ns, &young_trace_ns);

    total_scan_roots_time += scan_roots_ns;
    total_scan_rs_time += scan_rs_ns;
    total_scavenge_time += young_trace_ns;

    // scan_init_area();
    // printf("scavenge finished\n");

    giy_weak_clear(ctx);
    // printf("weak_clear finished\n");


    // for (int i = 0; i < FUNCTION_TABLE_LIMIT; i++) {
    //     FunctionTable *p = &ctx->function_table[i];
    //     for (int j = 0; j < p->n_insns; j++) {
    //         InlineCache *ic = &p->insns[j].inl_cache;
    //         if (ic->pm == NULL) {
    //             ic->prop_name = JS_EMPTY; 
    //         }
    //     }
    // }

    //     for (int i = 0; i < FUNCTION_TABLE_LIMIT; i++) {
    //     FunctionTable *p = &ctx->function_table[i];
    //     for (int j = 0; j < p->n_insns; j++) {
    //         InlineCache *ic = &p->insns[j].inl_cache;
    //         ic->pm = NULL;
    //         ic->prop_name = JS_EMPTY;
    //         // -------------------------------
    //     }
    // }







    // Entire young work area is reclaimed after successful minor GC.
    cache_space.current = cache_space.work_begin;
    print_dram_space_usage();
    print_cache_space_useage();




    #ifdef USE_REMEMBERED_SET
    rememberset_clear();
    // printf("remembered set cleared\n");
    #endif

    in_minor_gc = 0;
    
    pass_the_remember_set_count++;

    gc_pmu_stop_window();

    clock_gettime(CLOCK_MONOTONIC, &gc_wall_end);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &gc_cpu_end);
    total_gc_full_wall_time += timespec_diff_ns(gc_wall_start, gc_wall_end);
    total_gc_full_cpu_time += timespec_diff_ns(gc_cpu_start, gc_cpu_end);

    // printf("minor GC finished\n");
    // printf("Total passed the remembered set count: %ld\n", pass_the_remember_set_count);
    return;
}

void scavenge(){
    while (dram_space.current < dram_space.free){
        if(dram_space.current >= dram_space.end){
            printf("Error: dram_space.current exceed dram_space.end\n");
            exit(1);
        }
        uintptr_t hdr_ptr = dram_space.current;
        object_header *obj_hdr = (object_header *) hdr_ptr;
        dram_space.current += ALIGN(obj_hdr->size + sizeof(object_header));
        uintptr_t payload_ptr = hdr_ptr + sizeof(object_header);
        // printf("Scavenging object of size %d bytes at dram address %p, type 0x%02x\n", obj_hdr->size, (void*)payload_ptr, obj_hdr->type);
        //scan the object fields
        process_node<GiYIsolatedTracer>(obj_hdr->type, payload_ptr);
    }
}

void scan_init_area() {
    uintptr_t current = cache_space.begin;
    while (current < cache_space.work_begin) {
        object_header *obj_hdr = (object_header *) current;
        uintptr_t payload_ptr = current + sizeof(object_header);
        
        process_node<GiYIsolatedTracer>(obj_hdr->type, payload_ptr);
        
        current += ALIGN(obj_hdr->size + sizeof(object_header));
        scan_init_objects_count++;
    }
    // printf("scan_init_area finished\n");
}

void scan_remembered_set() {
    for (int i = 0; i < remembered_set.count; i++) {
        uintptr_t raw_slot = remembered_set.buffer[i];
        bool is_ptr_slot = (raw_slot & 1) != 0;
        uintptr_t slot_addr = raw_slot & ~(uintptr_t)1;

        bool in_dram = (slot_addr >= dram_space.begin && slot_addr < dram_space.end);
        bool in_init = (slot_addr >= cache_space.begin && slot_addr < cache_space.work_begin);
        if (!in_dram && !in_init) {
            continue;
        }

        if (is_ptr_slot) {
            void **slot = (void **) slot_addr;
            void *value = *slot;
            GiYIsolatedTracer::process_edge(value);
            *slot = value;
        } else {
            JSValue *slot = (JSValue *) slot_addr;
            GiYIsolatedTracer::process_edge(*slot);
        }
    }
}

static void print_gc_status(){
    // Capture end time for overall profiling
    clock_gettime(CLOCK_MONOTONIC, &program_end_wall_time);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &program_end_cpu_time);
    
    printf("\n");
    printf("========================================\n");
    printf("===   Performance Analysis Report    ===\n");
    printf("========================================\n\n");
    
    // Calculate total elapsed wall/cpu time
    double total_elapsed_wall = timespec_diff_ns(program_start_wall_time, program_end_wall_time) / 1e9;
    double total_elapsed_cpu = timespec_diff_ns(program_start_cpu_time, program_end_cpu_time) / 1e9;

    // Core breakdown time (roots/RS/traverse) and full-enclosed GC time
    long long total_gc_core_ns = total_scan_roots_time + total_scan_rs_time + total_scavenge_time;
    double total_gc_core_time = total_gc_core_ns / 1e9;
    double total_gc_full_wall = total_gc_full_wall_time / 1e9;
    double total_gc_full_cpu = total_gc_full_cpu_time / 1e9;

    double business_wall = total_elapsed_wall - total_gc_full_wall;
    double business_cpu = total_elapsed_cpu - total_gc_full_cpu;
    if (business_wall < 0.0)
        business_wall = 0.0;
    if (business_cpu < 0.0)
        business_cpu = 0.0;
    
    printf("=== Overall Runtime (Wall Clock) ===\n");
    printf("Total execution:     %.3f sec (100.0%%)\n", total_elapsed_wall);
    printf("  Business logic:    %.3f sec (%.1f%%)\n",
           business_wall,
           total_elapsed_wall > 0.0 ? (100.0 * business_wall / total_elapsed_wall) : 0.0);
    printf("  GC overhead(full): %.3f sec (%.1f%%)\n",
           total_gc_full_wall,
           total_elapsed_wall > 0.0 ? (100.0 * total_gc_full_wall / total_elapsed_wall) : 0.0);

    printf("\n=== Overall Runtime (CPU Time) ===\n");
    printf("Total CPU time:      %.3f sec (100.0%%)\n", total_elapsed_cpu);
    printf("  Business CPU:      %.3f sec (%.1f%%)\n",
           business_cpu,
           total_elapsed_cpu > 0.0 ? (100.0 * business_cpu / total_elapsed_cpu) : 0.0);
    printf("  GC CPU(full):      %.3f sec (%.1f%%)\n",
           total_gc_full_cpu,
           total_elapsed_cpu > 0.0 ? (100.0 * total_gc_full_cpu / total_elapsed_cpu) : 0.0);
    
    printf("\n=== GC Statistics ===\n");
    printf("Minor GC count:      %d\n", minor_gc_count);
    if (minor_gc_count > 0) {
        printf("Avg GC pause(full):  %.3f ms\n", 1000.0 * total_gc_full_wall / minor_gc_count);
        printf("GC frequency:        %.1f GC/sec\n", total_elapsed_wall > 0.0 ? (minor_gc_count / total_elapsed_wall) : 0.0);
    }
    printf("Init area objects:   %ld (avg %.1f per GC)\n", 
           scan_init_objects_count,
           (float)scan_init_objects_count / (minor_gc_count > 0 ? minor_gc_count : 1));
    
    #ifdef USE_REMEMBERED_SET
    printf("\n=== Remembered Set ===\n");
    printf("Write barrier calls: %ld\n", write_barrier_calls);
    if (write_barrier_calls > 0 && minor_gc_count > 0) {
        printf("  Duplicates:        %ld (%.1f%%)\n", 
               write_barrier_duplicate_filtered,
               100.0 * write_barrier_duplicate_filtered / write_barrier_calls);
        printf("  Avg per GC:        %.1f\n", (float)write_barrier_calls / minor_gc_count);
    } else if (write_barrier_calls > 0) {
        printf("  Duplicates:        %ld (%.1f%%)\n", 
               write_barrier_duplicate_filtered,
               100.0 * write_barrier_duplicate_filtered / write_barrier_calls);
        printf("  Avg per GC:        N/A (no GC yet)\n");
    }
    #endif

    #if GIY_WB_PROFILE
    giy_print_wb_profile();
    #endif
    
    printf("\n=== Allocation Statistics ===\n");
    printf("Total allocations:   %lld\n", generational_alloc_count);
    printf("Total alloc bytes:   %.2f MB\n", generational_alloc_bytes / (1024.0 * 1024.0));
    if (generational_alloc_count > 0) {
        printf("Avg alloc size:      %.1f bytes\n", (double)generational_alloc_bytes / generational_alloc_count);
        if (minor_gc_count > 0)
            printf("Allocs per GC:       %.1f\n", (double)generational_alloc_count / minor_gc_count);
        else
            printf("Allocs per GC:       N/A (no GC yet)\n");
    }
    printf("Forward operations:  %lld\n", generational_forward_count);
    if (generational_forward_count > 0 && minor_gc_count > 0) {
        printf("  Forward per GC:    %.1f\n", (double)generational_forward_count / minor_gc_count);
    } else if (generational_forward_count > 0) {
        printf("  Forward per GC:    N/A (no GC yet)\n");
    }

    #if ALLOC_SIZE_PROFILE
    alloc_size_profile_print();
    #endif
    
    if (minor_gc_count > 0 && total_gc_core_ns > 0) {
        printf("\n=== GC Core Breakdown (Roots/RS/Traverse) ===\n");
        printf("GC core total:       %.3f sec\n", total_gc_core_time);
        printf("scan_roots:          %.3f sec (%.1f%% of core)\n",
               total_scan_roots_time / 1e9,
               100.0 * total_scan_roots_time / total_gc_core_ns);
        printf("scan_RS:             %.3f sec (%.1f%% of core)\n",
               total_scan_rs_time / 1e9,
               100.0 * total_scan_rs_time / total_gc_core_ns);
        printf("scavenge:            %.3f sec (%.1f%% of core)\n",
               total_scavenge_time / 1e9,
               100.0 * total_scavenge_time / total_gc_core_ns);
    }

    giy_print_profile();

    printf("\n=== GC Cache Miss Counters (PMU) ===\n");
    if (!gc_pmu_supported) {
        printf("PMU counters:        unavailable on this machine/config\n");
#ifdef __linux__
        if (gc_pmu_perf_event_paranoid != -999)
            printf("perf_event_paranoid:%d\n", gc_pmu_perf_event_paranoid);
        if (gc_pmu_init_errno != 0)
            printf("PMU init errno:      %d (%s)\n", gc_pmu_init_errno, strerror(gc_pmu_init_errno));
        printf("hint:                run as root or set kernel.perf_event_paranoid<=1\n");
#endif
    } else {
        printf("PMU-sampled GC:      %d / %d\n", gc_pmu_sampled_gc_count, minor_gc_count);
        if (gc_pmu_fd_cache_misses >= 0) {
            printf("cache-misses:        %lld\n", total_gc_cache_misses);
            if (minor_gc_count > 0)
                printf("  Avg per GC:        %.1f\n", (double) total_gc_cache_misses / minor_gc_count);
        }
        if (gc_pmu_fd_cache_references >= 0)
            printf("cache-references:    %lld\n", total_gc_cache_references);
        if (gc_pmu_fd_cache_misses >= 0 && gc_pmu_fd_cache_references >= 0 && total_gc_cache_references > 0)
            printf("cache-miss rate:     %.2f%%\n", 100.0 * total_gc_cache_misses / total_gc_cache_references);
        if (gc_pmu_fd_instructions >= 0)
            printf("instructions:        %lld\n", total_gc_instructions);
        if (gc_pmu_fd_cache_misses >= 0 && gc_pmu_fd_instructions >= 0 && total_gc_instructions > 0)
            printf("cache MPKI:          %.3f\n", 1000.0 * total_gc_cache_misses / total_gc_instructions);
    }
    
    printf("\n========================================\n");
}


extern "C" void space_free_dram_manager_init(){
    printf("init_info: init object alloc over, now used bytes in cache space: %f KB\n",
           (float)(cache_space.current - cache_space.begin)/1024.0);
    cache_space.work_begin = cache_space.current;
#ifdef USE_GIY_MINOR
    // Bind GiY traversal stack inside cache space after work_begin is fixed.
    giy_bind_stack_to_cache();
#endif
    init_finish = 1;
    printf("init_info: after init object alloc, cache_space.work_begin moved to %p\n",
           (void*)cache_space.work_begin);    
    gc_pmu_init_once();
    // Start profiling timer after initialization
    clock_gettime(CLOCK_MONOTONIC, &program_start_wall_time);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &program_start_cpu_time);
}

extern "C" void space_print_memory_status() {
    printf("========== Memory Status ==========\n");
    print_cache_space_useage();
    print_dram_space_usage();
    printf("===================================\n");
}
