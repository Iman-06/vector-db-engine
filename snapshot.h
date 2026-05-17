#ifndef SNAPSHOT_H
#define SNAPSHOT_H
#include "vector_store.h"
#include "ivf.h"
#include <string>
// Returns 0 on success, negative on error.
int snapshot_save(const vector_store_t* vs, const ivf_index_t* ivf, const std::string& path);
// Returns 0 on success, negative on error. Populates vs and ivf.
int snapshot_load(vector_store_t* vs, ivf_index_t* ivf, const std::string& path);
#define SNAP_OK          0
#define SNAP_ERR_IO     -1
#define SNAP_ERR_MAGIC  -2
#define SNAP_ERR_VER    -3
#define SNAP_ERR_NOMEM  -4

#endif