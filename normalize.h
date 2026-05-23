#ifndef NORMALIZE_H
#define NORMALIZE_H

#include <vector>
#include <cstddef>

// Return codes
#define NORM_OK           0
#define NORM_ERR_ZERO    -1   // zero vector — cannot normalize

// Normalize a raw float array IN PLACE to unit length.
// Returns NORM_OK on success, NORM_ERR_ZERO if the vector is a zero vector.
int normalize_vector(float* vec, int dim);

// Normalize a std::vector<float> IN PLACE.
// Returns NORM_OK on success, NORM_ERR_ZERO if the vector is a zero vector.
int normalize_vec(std::vector<float>& vec);

// Returns the L2 norm (magnitude) of a vector.
double vec_norm(const float* vec, int dim);

#endif