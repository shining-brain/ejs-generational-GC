#ifndef CACHE_DRAM_MANAGER_H
#define CACHE_DRAM_MANAGER_H
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "header.h"

#define ALIGN_MASK (sizeof(uintptr_t) - 1)
#define ALIGN(s)   (((s) + ALIGN_MASK) & ~ALIGN_MASK)

#define DEFAULT_GC_THRESHOLD(heap_limit) (0)
// #define remember_set
// #define printf(...) do { } while (0)

static bool init_finish = 0;

typedef struct
{
    uintptr_t begin;
    uintptr_t work_begin;
    uintptr_t current;
    uintptr_t end;
    int total_size;
    int threshold_size;

} Cache_space;

typedef struct RememberedSet
{
    uintptr_t *buffer;  // Array of pointers to remembered objects
    int count;       // Number of remembered objects
    int capacity;    // Capacity of the buffer
} RememberedSet;

typedef struct
{
    uintptr_t begin;
    uintptr_t free;
    uintptr_t current;
    uintptr_t end;
    int total_size;
    int available_bytes;
} Dram_space;

extern Cache_space cache_space;
extern Dram_space dram_space;
extern RememberedSet remembered_set;


//current size: 16 bytes
typedef struct{
    uintptr_t forwarding_pointer;
    cell_type_t type;
    int size; // in bytes
    //align to 8 bytes
    // uintptr_t padding;
    // int padding2;
} object_header; 

// typedef struct object_header header_t;

// extern void space_init(size_t bytes, size_t threshold_bytes);
// extern void *space_alloc(uintptr_t request_bytes, cell_type_t type);
// // extern void space_free_dram_manager_init();
// extern "C" void space_print_memory_status();


#ifdef __cplusplus
extern "C" {
#endif

extern void space_init(size_t bytes, size_t threshold_bytes);
extern void *space_alloc(uintptr_t request_bytes, cell_type_t type);
extern void space_print_memory_status();
extern void init_remembered_set();
extern void rememberset_add(uintptr_t obj_ptr);
extern void rememberset_clear();
extern void write_barrier(JSValue *ptr, JSValue value);



#ifdef __cplusplus
}
#endif







static inline cell_type_t space_get_cell_type(uintptr_t ptr) {
    // printf("space_get_cell_type called\n");
    return ((object_header*)ptr - 1)->type;
}

static inline int space_check_gc_request()
{
    if(init_finish)
        printf("space_check_gc_request called\n");
        
    return cache_space.threshold_size > (cache_space.end - cache_space.current);
}

static inline int GC_PM_EQ(PropertyMap *p, PropertyMap *q)
{
    // printf("GC_PM_EQ called\n");
    return p == q;
}
#endif /* CACHE_DRAM_MANAGER_H_ */