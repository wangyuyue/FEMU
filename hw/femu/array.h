#ifndef __ARRAY_H__
#define __ARRAY_H__

typedef struct {
    int len;
    int elem_size;
    void* data;
} GenericArray;

typedef struct {
    int dim;
    int elem_size;
    void* data;
} Array1D;

typedef struct {
    int dim_o;
    int dim_i;
    int elem_size;
    void* data;
} Array2D;

static inline void* at_ith_1d(Array1D arr, int i) {
    assert (i < arr.dim);
    return (char*)arr.data + i * arr.elem_size;
}

static inline Array1D at_ith_2d(Array2D arr, int i) {
    assert (i < arr.dim_o);
    return (Array1D){arr.dim_i, arr.elem_size, (char*)arr.data + i * arr.dim_i * arr.elem_size};
}

static inline void* array_tail_2d(Array2D arr) {
    return (char*)arr.data + arr.dim_o * arr.dim_i * arr.elem_size;
}

static inline void* array_tail_1d(Array1D arr) {
    return (char*)arr.data + arr.dim * arr.elem_size;
}
#endif