#ifndef VECTOR_STORE_H
#define VECTOR_STORE_H
//h file for vector storage
#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

#define HASHMAP_INIT_CAP 1024

typedef struct hm_entry {
    int64_t  key;
    size_t   value;
    int      used;  //1 for occupied 0 for empty
} hm_entry_t;

typedef struct hashmap {
    hm_entry_t *buckets;
    size_t      cap;   //always a power of 2 as we grow it exponentially
    size_t      size;     //number of entries stored
} hashmap_t;

typedef struct vector_store {
    int      dim;
    //arrays for storage
    float   *vectors; 
    int64_t *ids;  
    size_t   count;  
    size_t   capacity;
    hashmap_t map;
    pthread_mutex_t lock;
} vector_store_t;

//return codes for different senerios
#define VS_OK            0
#define VS_ERR_NOMEM    -1
#define VS_ERR_BADDIM   -2


//one time initialization
int vs_init(vector_store_t *vs, int dim);
//free up all storage
void vs_destroy(vector_store_t *vs);
int vs_add(vector_store_t *vs, int64_t id, const float *comps);
//search
const float *vs_get_vector(const vector_store_t *vs, size_t i);
int64_t vs_get_id(const vector_store_t *vs, size_t i);
size_t vs_count(vector_store_t *vs);
//locking/unlocking
void vs_lock(vector_store_t *vs);
void vs_unlock(vector_store_t *vs);
#endif
