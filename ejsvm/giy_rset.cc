#include <stdlib.h>
#include <stdio.h>
#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "cache_dram_manager.h"

extern long write_barrier_calls;
extern long write_barrier_duplicate_filtered;
extern int in_minor_gc;

RememberedSet remembered_set;

#ifndef GIY_WB_PROFILE
#define GIY_WB_PROFILE 0
#endif

#if GIY_WB_PROFILE
static unsigned long long wb_profile_update_attempts = 0;
static unsigned long long wb_profile_update_hits = 0;
static unsigned long long wb_profile_update_scan_steps = 0;
static unsigned long long wb_profile_clear_attempts_jsvalue = 0;
static unsigned long long wb_profile_clear_attempts_ptr = 0;
static unsigned long long wb_profile_young_adds_jsvalue = 0;
static unsigned long long wb_profile_young_adds_ptr = 0;
static unsigned long long wb_profile_duplicate_value_updates = 0;
#endif

#define HASH_TABLE_SIZE 8192
#define HASH_PROBE_LIMIT 32
//only use the bits of 3-14 as hash value
#define MASK_HASH(ptr) (((ptr) >> 3) & (HASH_TABLE_SIZE - 1))
#define RS_PTR_SLOT_TAG ((uintptr_t)1)
#define REMEMBERED_SET_CAPACITY_BYTES (128 * 1024)

static int rememberset_find_index(uintptr_t obj_ptr) {
    for (int i = 0; i < remembered_set.count; i++) {
#if GIY_WB_PROFILE
        wb_profile_update_scan_steps++;
#endif
        if (remembered_set.buffer[i] == obj_ptr) {
            return i;
        }
    }
    return -1;
}

static bool rememberset_update_existing(uintptr_t obj_ptr, uintptr_t value) {
#if GIY_WB_PROFILE
    wb_profile_update_attempts++;
#endif
    int index = rememberset_find_index(obj_ptr);
    if (index < 0) {
        return false;
    }
#if GIY_WB_PROFILE
    wb_profile_update_hits++;
#endif
    remembered_set.values[index] = value;
    return true;
}

// Initialize the remembered set,and adjust cache_space.end accordingly, adjust total_size too
void init_remembered_set() {
    remembered_set.count = 0;

    // GiY stores both slot addresses and last written values in cache.
    remembered_set.capacity = REMEMBERED_SET_CAPACITY_BYTES / (int) (2 * sizeof(uintptr_t));
    // remembered_set.capacity = 24*1024/8*200; // Can remember 128*1024/8 = 600K objects
    remembered_set.values = (uintptr_t *)(cache_space.end - remembered_set.capacity * sizeof(uintptr_t));
    cache_space.end -= remembered_set.capacity * sizeof(uintptr_t);
    cache_space.total_size -= remembered_set.capacity * sizeof(uintptr_t);

    remembered_set.buffer = (uintptr_t *)(cache_space.end - remembered_set.capacity * sizeof(uintptr_t));
    cache_space.end -= remembered_set.capacity* sizeof(uintptr_t);
    cache_space.total_size -= remembered_set.capacity* sizeof(uintptr_t);

    // set up hash table for quick lookup
    remembered_set.size_of_hash_table = HASH_TABLE_SIZE;
    remembered_set.hash_table = (uintptr_t *)(cache_space.end - remembered_set.size_of_hash_table * sizeof(uintptr_t));
    cache_space.end -= remembered_set.size_of_hash_table * sizeof(uintptr_t);
    cache_space.total_size -= remembered_set.size_of_hash_table * sizeof(uintptr_t);

    memset(remembered_set.hash_table, 0, remembered_set.size_of_hash_table * sizeof(uintptr_t));
    
    printf("init_info: remembered set initialized with capacity %d , new cache_space.end at %p\n",
           remembered_set.capacity, (void*)cache_space.end);
    
}

static void rememberset_add_with_value(uintptr_t obj_ptr, uintptr_t value) {
    if (in_minor_gc) {
        return;
    }

    if (remembered_set.count >= remembered_set.capacity) {
        printf("Error: Remembered set full, cannot add more remembered objects!\n");
        exit(1);
    }


    unsigned int hash_idx = MASK_HASH(obj_ptr);
    
    
    for (int probe = 0; probe < HASH_PROBE_LIMIT; probe++) {
        unsigned int idx = (hash_idx + probe) & (HASH_TABLE_SIZE - 1);
        uintptr_t existing = remembered_set.hash_table[idx];
        
        if (existing == obj_ptr) {
#if GIY_WB_PROFILE
            wb_profile_duplicate_value_updates++;
#endif
            rememberset_update_existing(obj_ptr, value);
            write_barrier_duplicate_filtered++;
            return;
        }
        
        if (existing == 0) {
       
            remembered_set.hash_table[idx] = obj_ptr;
            remembered_set.buffer[remembered_set.count] = obj_ptr;
            remembered_set.values[remembered_set.count] = value;
            remembered_set.count += 1;
            return;
        }
    }
    
  
    remembered_set.buffer[remembered_set.count] = obj_ptr;
    remembered_set.values[remembered_set.count] = value;
    remembered_set.count += 1;
}

void rememberset_add(uintptr_t obj_ptr) {
    rememberset_add_with_value(obj_ptr, 0);
}

void rememberset_clear() {
    remembered_set.count = 0;
    memset(remembered_set.hash_table, 0, remembered_set.size_of_hash_table * sizeof(uintptr_t));
}

void write_barrier(JSValue* ptr, JSValue value){
    if (in_minor_gc) {
        return;
    }

    uintptr_t obj_ptr = (uintptr_t)ptr;
    if (obj_ptr >= cache_space.work_begin && obj_ptr < cache_space.end) {
        return;
    }
    bool slot_in_dram = (obj_ptr >= dram_space.begin && obj_ptr < dram_space.end);
    bool slot_in_init = (obj_ptr >= cache_space.begin && obj_ptr < cache_space.work_begin);
    if (!slot_in_dram && !slot_in_init) {
        return;
    }

    if (is_fixnum(value) || is_special(value)) {
#if GIY_WB_PROFILE
        wb_profile_clear_attempts_jsvalue++;
#endif
        rememberset_update_existing(obj_ptr, 0);
        return;
    }

    uintptr_t obj_addr = clear_ptag(value);
    if (obj_addr < cache_space.work_begin || obj_addr >= cache_space.end) {
#if GIY_WB_PROFILE
        wb_profile_clear_attempts_jsvalue++;
#endif
        rememberset_update_existing(obj_ptr, 0);
        return;
    }

#if GIY_WB_PROFILE
    wb_profile_young_adds_jsvalue++;
#endif
    write_barrier_calls++;
    rememberset_add_with_value(obj_ptr, (uintptr_t) value);
}

void write_barrier_ptr(void** ptr, void* value){
    if (in_minor_gc) {
        return;
    }

    uintptr_t obj_ptr = (uintptr_t)ptr;
    if (obj_ptr >= cache_space.work_begin && obj_ptr < cache_space.end) {
        return;
    }
    bool slot_in_dram = (obj_ptr >= dram_space.begin && obj_ptr < dram_space.end);
    bool slot_in_init = (obj_ptr >= cache_space.begin && obj_ptr < cache_space.work_begin);
    if (!slot_in_dram && !slot_in_init) {
        return;
    }

    uintptr_t val_ptr = (uintptr_t)value;
    if (val_ptr == 0 ||
        val_ptr < cache_space.work_begin ||
        val_ptr >= cache_space.end) {
#if GIY_WB_PROFILE
        wb_profile_clear_attempts_ptr++;
#endif
        rememberset_update_existing(obj_ptr | RS_PTR_SLOT_TAG, 0);
        return;
    }

#if GIY_WB_PROFILE
    wb_profile_young_adds_ptr++;
#endif
    write_barrier_calls++;
    rememberset_add_with_value(obj_ptr | RS_PTR_SLOT_TAG, val_ptr);
}

#if GIY_WB_PROFILE
extern "C" void giy_print_wb_profile() {
    printf("\n=== GiY Write Barrier Profile ===\n");
    printf("Update attempts:     %llu\n", wb_profile_update_attempts);
    printf("Update hits:         %llu\n", wb_profile_update_hits);
    printf("Update scan steps:   %llu\n", wb_profile_update_scan_steps);
    if (wb_profile_update_attempts > 0) {
        printf("  Steps per attempt: %.3f\n",
               (double) wb_profile_update_scan_steps /
               (double) wb_profile_update_attempts);
    }
    printf("Clear attempts JS:   %llu\n", wb_profile_clear_attempts_jsvalue);
    printf("Clear attempts ptr:  %llu\n", wb_profile_clear_attempts_ptr);
    printf("Young adds JS:       %llu\n", wb_profile_young_adds_jsvalue);
    printf("Young adds ptr:      %llu\n", wb_profile_young_adds_ptr);
    printf("Duplicate value updates: %llu\n", wb_profile_duplicate_value_updates);
}
#endif
