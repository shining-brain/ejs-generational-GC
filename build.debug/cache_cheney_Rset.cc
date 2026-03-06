#include <stdlib.h>
#include <stdio.h>
#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "cache_dram_manager.h"

extern long write_barrier_calls;
extern long write_barrier_duplicate_filtered;

RememberedSet remembered_set;

#define HASH_TABLE_SIZE 4096  // 增加到4K个槽位减少冲突
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

    // 使用hash table快速去重
    unsigned int hash_idx = MASK_HASH(obj_ptr);
    
    // 简单的线性探测处理冲突
    for (int probe = 0; probe < 8; probe++) {
        unsigned int idx = (hash_idx + probe) & (HASH_TABLE_SIZE - 1);
        uintptr_t existing = remembered_set.hash_table[idx];
        
        if (existing == obj_ptr) {
            // 已存在,直接返回
            write_barrier_duplicate_filtered++;
            return;
        }
        
        if (existing == 0) {
            // 找到空槽位,添加新条目
            remembered_set.hash_table[idx] = obj_ptr;
            remembered_set.buffer[remembered_set.count] = obj_ptr;
            remembered_set.count += 1;
            return;
        }
        // 否则继续探测下一个位置
    }
    
    // 探测失败,仍然添加(hash table已满,但buffer还有空间)
    remembered_set.buffer[remembered_set.count] = obj_ptr;
    remembered_set.count += 1;
}   

void rememberset_clear() {
    remembered_set.count = 0;
    memset(remembered_set.hash_table, 0, remembered_set.size_of_hash_table * sizeof(uintptr_t));
}

void write_barrier(JSValue* ptr, JSValue value){
    // 快速路径: 先检查最常见的情况
    
    // 1. 检查value是否在young generation (最常见的早期返回)
    uintptr_t obj_addr = (uintptr_t)value;
    if (obj_addr < cache_space.work_begin || obj_addr >= cache_space.end) {
        // value不在cache中,不需要记录
        return;
    }
    
    // 2. 检查slot是否在old generation
    uintptr_t obj_ptr = (uintptr_t)ptr;
    if(obj_ptr >= cache_space.work_begin && obj_ptr < cache_space.end){
        // slot在cache中, cache->cache引用,不需要记录
        return;
    }
    
    // 3. 检查是否是对象引用
    if(!is_object(value)){
        return;
    }
    
    write_barrier_calls++;  // 只统计真正需要处理的调用
    
    // 使用clear_ptag获取真实地址
    obj_addr = clear_ptag(value);
    if (obj_addr < cache_space.work_begin || obj_addr >= cache_space.end) {
        return;
    }
    
    rememberset_add(obj_ptr);
}

void write_barrier_ptr(void** ptr, void* value){
    // 快速路径: 先检查最常见的情况
    
    // 1. 检查value是否在young generation
    uintptr_t val_ptr = (uintptr_t)value;
    if(val_ptr < cache_space.work_begin || val_ptr >= cache_space.end){
        // value不在cache中
        return;
    }
    
    // 2. 检查slot是否在old generation
    uintptr_t obj_ptr = (uintptr_t)ptr;
    if(obj_ptr >= cache_space.work_begin && obj_ptr < cache_space.end){
        // slot在cache中, cache->cache引用
        return;
    }
    
    write_barrier_calls++;  // 只统计真正需要处理的调用
    
    rememberset_add(obj_ptr);
}

