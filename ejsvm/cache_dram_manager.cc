#include <stdlib.h>
#include <stdio.h>
#include "prefix.h"
#define EXTERN extern
#include "header.h"
#include "log.h"

static Context *the_context;
#include "gc-visitor-inl.h"
#include "cache_dram_manager.h"


Cache_space cache_space;
Dram_space dram_space;

int initial_alloc_bytes;
int minor_gc_count = 0;
long pass_the_remember_set_count = 0;

void scavenge();
void scan_init_area();
void scan_remembered_set();

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
    printf("DRAM space usage: used %d KB, available bytes: %d KB, total %d KB\n", used_bytes / 1024, dram_space.available_bytes / 1024, dram_space.total_size / 1024);
}

void print_cache_space_useage()
{
    int used_bytes = cache_space.current - cache_space.work_begin;
    printf("Cache space usage: used %f KB\n", used_bytes / 1024.0);
}

class CacheCheney_Tracer
{
private:

    static bool in_cache_space(uintptr_t ptr) {
        return cache_space.work_begin <= ptr && ptr < cache_space.end;
    }

    static bool in_dram_space(uintptr_t ptr) {
        return dram_space.begin <= ptr && ptr < dram_space.begin + dram_space.total_size;
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
        // printf("process JSValue &v edge\n");
        if (is_fixnum(v) || is_special(v))
            return;


        uintptr_t ptr = (uintptr_t) clear_ptag(v);
        
        //use remember set
        # ifdef remember_set
        if (!in_cache_space(ptr))
            return;
        # endif

        pass_the_remember_set_count++;
        uintptr_t to = forward(ptr);
        PTag tag = get_ptag(v);
        v = put_ptag(to, tag);
    }
    static void process_edge(void *&p) {
        // printf("process void *&p edge\n");
        uintptr_t ptr = (uintptr_t) p;

        //use remember set
        # ifdef remember_set
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
        # ifdef remember_set
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
        # ifdef remember_set
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
        # ifdef remember_set
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
        # ifdef remember_set
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

    CacheCheney_Tracer(/* args */);
    ~CacheCheney_Tracer
();
};

CacheCheney_Tracer::CacheCheney_Tracer(/* args */)
{
}

CacheCheney_Tracer::~CacheCheney_Tracer()
{
}



void space_init(size_t bytes, size_t threshold_bytes)
{

    printf("object_header size: %lu bytes\n", sizeof(object_header));
    initial_alloc_bytes = 0;
    //initial cache space
    long cache_size = 512*1024; // 512KB cache space
    cache_space.begin = (uintptr_t) malloc(cache_size); // 512KB cache space
    cache_space.current = cache_space.begin;
    cache_space.total_size = cache_size;
    cache_space.threshold_size = threshold_bytes;

    //test
    cache_space.threshold_size = 1024; // 1KB threshold for testing

    cache_space.end = cache_space.begin + cache_space.total_size;

    init_remembered_set();



   

    // printf("Cache space allocated at address %p, to %p\n", (void*)cache_space.begin, (void*)(cache_space.begin + cache_space.total_size));
    printf("init info: cache_space.begin=%p, cache_space.end=%p, total_size=%dKB, threshold_size=%dMB\n",
           (void*)cache_space.begin, (void*)cache_space.end, cache_space.total_size/1024, cache_space.threshold_size/1024/1024);


    //initial dram space
    dram_space.begin = (uintptr_t) malloc(bytes);
    dram_space.free = dram_space.begin;
    dram_space.current = dram_space.begin;
    dram_space.total_size = bytes;
    dram_space.available_bytes = bytes;
    dram_space.end = dram_space.begin + dram_space.total_size;

    printf("Now we are using cache_cheney GC. Cache DRAM Manager initialized: Cache size %d Mbytes, DRAM size %d Mbytes\n",
           cache_space.total_size / (1024 * 1024), dram_space.total_size / (1024 * 1024));
    printf("DRAM address info: begin=%p, end=%p, total_size=%dKB\n",
           (void*)dram_space.begin, (void*)dram_space.end, dram_space.total_size/1024);

    print_dram_space_usage();
}

void *space_alloc(uintptr_t request_bytes, cell_type_t type)
{
    
    // printf("Cache DRAM Manager allocating %lu bytes, type is %#x\n", request_bytes, type);
    // printf("allocate at address %p\n", (void*) (cache_space.current + sizeof(object_header)));


    if (request_bytes == 0) {
        assert(type == CELLT_ARRAY_DATA);
        JSValue *array = (JSValue *) space_alloc(sizeof(JSValue), type);
        array[0] = JS_UNDEFINED;
        return array;
    }


    int total_byte = (int)request_bytes + sizeof(object_header);
    int align_bytes = ALIGN(total_byte);
    // if(cache_space.current + align_bytes > cache_space.end) {
    //     printf("Cache space full, need to trigger GC\n");
    //     exit(1);
    //     // garbage_collection(the_context);
    // }

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
    
    dram_space.current = dram_space.begin;

    if(need_major_gc()) {
        printf("===============major GC triggered=================\n");
        //todo: implement major GC
        exit(1);
    }

    print_cache_space_useage();
    printf("=================minor GC triggered==================\n");
    minor_gc_count++;
    printf("Minor GC count: %d, size of rememberset %d\n", minor_gc_count, remembered_set.count);

    scan_roots<CacheCheney_Tracer>(ctx);

    scan_init_area();

    #ifdef remember_set
    scan_remembered_set();
    #endif
        pass_the_remember_set_count++;
    scavenge();
    printf("scavenge finished\n");

    // weak_clear<CacheCheney_Tracer>(ctx);
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







    cache_space.current = cache_space.work_begin;
    print_dram_space_usage();
    print_cache_space_useage();




    #ifdef remember_set
    rememberset_clear();
    printf("remembered set cleared\n");
    #endif
    
    pass_the_remember_set_count++;
    printf("minor GC finished\n");
    printf("Total passed the remembered set count: %ld\n", pass_the_remember_set_count);
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
        process_node<CacheCheney_Tracer>(obj_hdr->type, payload_ptr);
    }
}

void scan_init_area() {
    uintptr_t current = cache_space.begin;
    while (current < cache_space.work_begin) {
        object_header *obj_hdr = (object_header *) current;
        uintptr_t payload_ptr = current + sizeof(object_header);
        
        process_node<CacheCheney_Tracer>(obj_hdr->type, payload_ptr);
        
        current += ALIGN(obj_hdr->size + sizeof(object_header));
    }
    printf("scan_init_area finished\n");
}

void scan_remembered_set() {
    for (int i = 0; i < remembered_set.count; i++) {
        uintptr_t slot_addr = remembered_set.buffer[i];
        JSValue* slot = (JSValue*) slot_addr;
        // if(slot_addr < cache_space.work_begin || slot_addr >= cache_space.end){
        //     printf("INVALID ADDRESS in remembered set: %p, skip it\n", (void*)slot_addr);
        //     continue;
        // }

        // printf("value=%lx\n", *slot);
        CacheCheney_Tracer::process_edge(*slot);
    }
    printf("scan_remembered_set finished, processed %d remembered objects\n", remembered_set.count);
    
}


extern "C" void space_free_dram_manager_init(){
    printf("init_info: init object alloc over, now used bytes in cache space: %f KB\n",
           (float)(cache_space.current - cache_space.begin)/1024.0);
    cache_space.work_begin = cache_space.current;
    init_finish = 1;
    printf("init_info: after init object alloc, cache_space.work_begin moved to %p\n",
           (void*)cache_space.work_begin);
}

extern "C" void space_print_memory_status() {
    printf("========== Memory Status ==========\n");
    print_cache_space_useage();
    print_dram_space_usage();
    printf("===================================\n");
}