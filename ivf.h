#ifndef IVF_H
#define IVF_H
#include "vector_store.h"
#include "kmeans.h"
#include <vector>
#include <mutex> // mutex for multithreaded access to the index
#include <cstdint> 
using namespace std;
struct ivf_index_t {
    bool built = false;
    int k = 0;                        
    int dim = 0;                       
    int iterations = 0;                 // how many kmeans iterations ran
    double build_time_seconds = 0.0;    // time for BUILD
    vector<float> centroids;       //centroid c starts at c * dim
    vector<vector<int>> clusters; // clusters[c] = list of internal store indices
};
// Caller must hold the store mutex before calling this.
int ivf_build(const vector_store_t& store, ivf_index_t& index);
// After BUILD insert a newly added vector (at internal index new_idx) into
// the nearest cluster. Caller must hold the store mutex.
void ivf_insert(ivf_index_t& index, const float* vec, int new_idx);
#endif