#include <stddef.h>
typedef struct {
    int size;
    float* data;
} EmbeddingTable;

extern EmbeddingTable* tables;
extern int dim_vec;

#define DIM_EMB 128
#define EMB_SIZE (DIM_EMB * sizeof(float))

#define PAGE_SIZE 512
#define PAGE_ALIGN(x) ((x) & ~(PAGE_SIZE - 1))
#define PAGE_OFFSET(x) ((x) & (PAGE_SIZE - 1))

EmbeddingTable* init_tables(int n_table, int table_size[], size_t addr, size_t size);

int cxl_lookup(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx);

int recssd_lookup(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx);

void simd_add(float* sum, float* vector);

#if 0
#define dlrm_debug(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
#else
#define dlrm_debug(fmt, ...) do { } while (0)
#endif