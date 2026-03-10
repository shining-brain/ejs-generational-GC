#include <stdlib.h>
#include <stdio.h>
#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "cache_dram_manager.h"

extern long write_barrier_calls;
extern long write_barrier_duplicate_filtered;

RememberedSet remembered_set;

#define HASH_TABLE_SIZE 4096  
//only use the bits of 3-14 as hash value
#define MASK_HASH(ptr) (((ptr) >> 3) & (HASH_TABLE_SIZE - 1))

// Initialize the remembered set,and adjust cache_space.end accordingly, adjust total_size too
void init_remembered_set() {
    remembered_set.count = 0;
    remembered_set.capacity = 24*1024/8; // Can remember 24*1024/8 = 3K objects
    // remembered_set.capacity = 24*1024/8*200; // Can remember 128*1024/8 = 600K objects
    remembered_set.buffer = (uintptr_t *)(cache_space.end - remembered_set.capacity* sizeof(uintptr_t));
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

void rememberset_add(uintptr_t obj_ptr) {
    if (remembered_set.count >= remembered_set.capacity) {
        printf("Error: Remembered set full, cannot add more remembered objects!\n");
        exit(1);
    }


    unsigned int hash_idx = MASK_HASH(obj_ptr);
    
    
    for (int probe = 0; probe < 8; probe++) {
        unsigned int idx = (hash_idx + probe) & (HASH_TABLE_SIZE - 1);
        uintptr_t existing = remembered_set.hash_table[idx];
        
        if (existing == obj_ptr) {
            
            write_barrier_duplicate_filtered++;
            return;
        }
        
        if (existing == 0) {
       
            remembered_set.hash_table[idx] = obj_ptr;
            remembered_set.buffer[remembered_set.count] = obj_ptr;
            remembered_set.count += 1;
            return;
        }
    }
    
  
    remembered_set.buffer[remembered_set.count] = obj_ptr;
    remembered_set.count += 1;
}   

void rememberset_clear() {
    remembered_set.count = 0;
    memset(remembered_set.hash_table, 0, remembered_set.size_of_hash_table * sizeof(uintptr_t));
}

void write_barrier(JSValue* ptr, JSValue value){
   
    
  
    uintptr_t obj_addr = (uintptr_t)value;
    if (obj_addr < cache_space.work_begin || obj_addr >= cache_space.end) {
   
        return;
    }
    
    uintptr_t obj_ptr = (uintptr_t)ptr;
    if(obj_ptr >= cache_space.work_begin && obj_ptr < cache_space.end){
     
        return;
    }
    

    if(!is_object(value)){
        return;
    }
    
    write_barrier_calls++; 
    
   
    obj_addr = clear_ptag(value);
    if (obj_addr < cache_space.work_begin || obj_addr >= cache_space.end) {
        return;
    }
    
    rememberset_add(obj_ptr);
}

void write_barrier_ptr(void** ptr, void* value){
    
    uintptr_t val_ptr = (uintptr_t)value;
    if(val_ptr < cache_space.work_begin || val_ptr >= cache_space.end){
       
        return;
    }

    uintptr_t obj_ptr = (uintptr_t)ptr;
    if(obj_ptr >= cache_space.work_begin && obj_ptr < cache_space.end){

        return;
    }
    
    write_barrier_calls++; 
    
    rememberset_add(obj_ptr);
}

