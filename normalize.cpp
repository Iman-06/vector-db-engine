#include "normalize.h"
#include <cmath>
#include <vector>

double vec_norm(const float* vec, int dim) {
    double sum = 0.0;
    for (int i = 0; i < dim; i++)
        sum += (double)vec[i] * (double)vec[i];
    return sqrt(sum);
}

int normalize_vector(float* vec, int dim) {
    double norm = vec_norm(vec, dim);

    // treat anything smaller than this as a zero vector
    if (norm < 1e-10)
        return NORM_ERR_ZERO;

    for (int i = 0; i < dim; i++)
        vec[i] = (float)((double)vec[i] / norm);

    return NORM_OK;
}

int normalize_vec(std::vector<float>& vec) {
    return normalize_vector(vec.data(), (int)vec.size());
}