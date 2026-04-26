//c file for implementation
#include "vector_store.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
static size_t hm_hash(int64_t key, size_t cap) {
    uint64_t x = (uint64_t)key;
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    x =  x ^ (x >> 31);
    return (size_t)(x & (cap - 1));
}

//Allocate a fresh bucket array of size `cap`
static hm_entry_t *hm_alloc_buckets(size_t cap) {
    hm_entry_t *b = calloc(cap, sizeof(hm_entry_t));
    return b;
}
static void hm_insert_raw(hm_entry_t *buckets, size_t cap,
                           int64_t key, size_t value) {
    size_t idx = hm_hash(key, cap);
    for (;;) {
        if (!buckets[idx].used) {
            buckets[idx].key   = key;
            buckets[idx].value = value;
            buckets[idx].used  = 1;
            return;
        }
        if (buckets[idx].key == key) {
            buckets[idx].value = value;
            return;
        }
        idx = (idx + 1) & (cap - 1);
    }
}
static int hm_grow(hashmap_t *m, size_t newcap) {
    hm_entry_t *nb = hm_alloc_buckets(newcap);
    if (!nb) return VS_ERR_NOMEM;

    for (size_t i = 0; i < m->cap; i++) {
        if (m->buckets[i].used)
            hm_insert_raw(nb, newcap, m->buckets[i].key, m->buckets[i].value);
    }
    free(m->buckets);
    m->buckets = nb;
    m->cap     = newcap;
    return VS_OK;
}
static int hm_maybe_grow(hashmap_t *m) {
    if (m->size * 10 >= m->cap * 7) {
        size_t newcap = m->cap * 2;
        return hm_grow(m, newcap);
    }
    return VS_OK;
}
static int hm_init(hashmap_t *m) {
    m->cap     = HASHMAP_INIT_CAP;
    m->size    = 0;
    m->buckets = hm_alloc_buckets(m->cap);
    return m->buckets ? VS_OK : VS_ERR_NOMEM;
}
static void hm_destroy(hashmap_t *m) {
    free(m->buckets);
    m->buckets = NULL;
    m->cap = m->size = 0;
}
static int hm_set(hashmap_t *m, int64_t key, size_t value) {
    int rc = hm_maybe_grow(m);
    if (rc != VS_OK) return rc;
    size_t idx = hm_hash(key, m->cap);
    for (;;) {
        if (!m->buckets[idx].used) break;
        if (m->buckets[idx].key == key) {
            m->buckets[idx].value = value;
            return VS_OK;
        }
        idx = (idx + 1) & (m->cap - 1);
    }
    //New key 
    m->buckets[idx].key   = key;
    m->buckets[idx].value = value;
    m->buckets[idx].used  = 1;
    m->size++;
    return VS_OK;
}
static int hm_get(const hashmap_t *m, int64_t key, size_t *out_value) {
    size_t idx = hm_hash(key, m->cap);
    for (;;) {
        if (!m->buckets[idx].used) return 0;
        if (m->buckets[idx].key == key) {
            *out_value = m->buckets[idx].value;
            return 1;
        }
        idx = (idx + 1) & (m->cap - 1);
    }
}
#define INITIAL_CAPACITY 64
static int vs_grow(vector_store_t *vs, size_t needed) {
    if (vs->capacity >= needed) return VS_OK;

    size_t newcap = vs->capacity ? vs->capacity : INITIAL_CAPACITY;
    while (newcap < needed) newcap *= 2;

    float   *nv = realloc(vs->vectors, newcap * (size_t)vs->dim * sizeof(float));
    int64_t *ni = realloc(vs->ids,     newcap * sizeof(int64_t));

    if (!nv || !ni) {
        if (ni) vs->ids     = ni;
        return VS_ERR_NOMEM;
    }

    vs->vectors  = nv;
    vs->ids      = ni;
    vs->capacity = newcap;
    return VS_OK;
}

int vs_init(vector_store_t *vs, int dim) {
    assert(dim > 0);
    memset(vs, 0, sizeof(*vs));
    vs->dim = dim;

    int rc = hm_init(&vs->map);
    if (rc != VS_OK) return rc;

    rc = vs_grow(vs, INITIAL_CAPACITY);
    if (rc != VS_OK) {
        hm_destroy(&vs->map);
        return rc;
    }

    if (pthread_mutex_init(&vs->lock, NULL) != 0) {
        hm_destroy(&vs->map);
        free(vs->vectors);
        free(vs->ids);
        return VS_ERR_NOMEM;
    }

    return VS_OK;
}

void vs_destroy(vector_store_t *vs) {
    pthread_mutex_destroy(&vs->lock);
    hm_destroy(&vs->map);
    free(vs->vectors);
    free(vs->ids);
    memset(vs, 0, sizeof(*vs));
}
int vs_add(vector_store_t *vs, int64_t id, const float *comps) {
    pthread_mutex_lock(&vs->lock);

    size_t idx;
    int exists = hm_get(&vs->map, id, &idx);

    if (exists) {
        memcpy(vs->vectors + idx * (size_t)vs->dim,
               comps,
               (size_t)vs->dim * sizeof(float));
        pthread_mutex_unlock(&vs->lock);
        return VS_OK;
    }
    int rc = vs_grow(vs, vs->count + 1);
    if (rc != VS_OK) {
        pthread_mutex_unlock(&vs->lock);
        return rc;
    }

    idx = vs->count;
    memcpy(vs->vectors + idx * (size_t)vs->dim,
           comps,
           (size_t)vs->dim * sizeof(float));
    vs->ids[idx] = id;
    rc = hm_set(&vs->map, id, idx);
    if (rc != VS_OK) {
        pthread_mutex_unlock(&vs->lock);
        return rc;
    }
    vs->count++;

    pthread_mutex_unlock(&vs->lock);
    return VS_OK;
}

const float *vs_get_vector(const vector_store_t *vs, size_t i) {
    if (i >= vs->count) return NULL;
    return vs->vectors + i * (size_t)vs->dim;
}

int64_t vs_get_id(const vector_store_t *vs, size_t i) {
    return vs->ids[i];
}

size_t vs_count(vector_store_t *vs) {
    pthread_mutex_lock(&vs->lock);
    size_t n = vs->count;
    pthread_mutex_unlock(&vs->lock);
    return n;
}

void vs_lock(vector_store_t *vs)   { pthread_mutex_lock(&vs->lock);   }
void vs_unlock(vector_store_t *vs) { pthread_mutex_unlock(&vs->lock); }
