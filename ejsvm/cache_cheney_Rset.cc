#include <stdlib.h>
#include <stdio.h>
#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "cache_dram_manager.h"


RememberedSet remembered_set;

// Initialize the remembered set,and adjust cache_space.end accordingly, adjust total_size too
void init_remembered_set() {
    remembered_set.count = 0;
    remembered_set.capacity = 24*1024/8; // Can remember 24*1024/8 = 3K objects
    remembered_set.buffer = (uintptr_t *)(cache_space.end - remembered_set.capacity* sizeof(uintptr_t));
    cache_space.end -= remembered_set.capacity* sizeof(uintptr_t);
    cache_space.total_size -= remembered_set.capacity* sizeof(uintptr_t);
    printf("init_info: remembered set initialized with capacity %d , new cache_space.end at %p\n",
           remembered_set.capacity, (void*)cache_space.end);
    
}

void rememberset_add(uintptr_t obj_ptr) {
    if (remembered_set.count >= remembered_set.capacity) {
        printf("Error: Remembered set full, cannot add more remembered objects!\n");
        exit(1);
    }

    //test: avoid duplicate entries
    //efficiency now is very low
    for(int i=0; i< remembered_set.count; i++){
        if(remembered_set.buffer[i] == obj_ptr){
            return;
        }
    }

    remembered_set.buffer[remembered_set.count] = obj_ptr;
    remembered_set.count += 1;
    // printf("remembered set add: added object at %p, total remembered objects: %d\n", (void*)obj_ptr, remembered_set.count);
}   

void rememberset_clear() {
    remembered_set.count = 0;
}

void write_barrier(JSValue* ptr, JSValue value){
    // printf("write_barrier called, slot = %p\n", (void*)ptr);
    uintptr_t obj_ptr = (uintptr_t)ptr;
    if(!is_object(value)){
            // Not a pointer, do nothing
            return;
        }
    if(obj_ptr >= cache_space.work_begin && obj_ptr < cache_space.end){
        // Inside cache, do nothing
        return;
    }
    uintptr_t obj_addr = clear_ptag(value);
    if (!(obj_addr >= cache_space.work_begin && obj_addr < cache_space.end)) {
        // Not pointing to young object, do nothing
        return;
    }
    // printf("write_barrier: Remembering object at %p pointing to young object at %p\n", (void*)obj_ptr, (void*)obj_addr);
    rememberset_add(obj_ptr);
}

