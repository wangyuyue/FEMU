#include "test_function.h"
#include "dlrm.h"
#include <assert.h>
#include <immintrin.h>
#include <stdio.h>

typedef struct QueryContext {
    int n_query;
    int n_indice;
    int data[];
} QueryContext;

inline int* get_offsets(QueryContext* query_ctx) {
    return query_ctx->data;
}

inline int* get_indices(QueryContext* query_ctx) {
    return query_ctx->data + query_ctx->n_query;
}

typedef struct TableContext {
    int n_table;
    int data[];
} TableContext;

typedef struct {
    int state;
    int data[];
} RecSSDContext;

EmbeddingTable* tables;

EmbeddingTable* init_tables(int n_table, int table_size[], size_t addr, size_t size) {
    EmbeddingTable *tables = malloc(sizeof(EmbeddingTable) * n_table);
    size_t offset = 0;
    for (int i = 0; i < n_table; i++) {
        tables[i].size = table_size[i];
        tables[i].data = (float*) (addr + offset);
        offset += tables[i].size;
        assert(offset <= size);
    }
    return tables;
}

void simd_add(float* sum, float* vector) {
    for (int l = 0; l < DIM_EMB; l += 8) { // Process 8 floats at a time
        __m256 vec_sum = _mm256_loadu_ps(&sum[l]); // Load sum[l] into AVX register
        __m256 vec_tbl = _mm256_loadu_ps(&vector[l]); // Load tbl[idx * DIM_EMB + l] into AVX register
        vec_sum = _mm256_add_ps(vec_sum, vec_tbl); // Perform vectorized sum
        _mm256_storeu_ps(&sum[l], vec_sum); // Store the result back to sum[l]
    }
}

inline float* get_cxl_vector(EmbeddingTable* table, int idx) {
    if (idx < table->size) {
        return table->data + idx * DIM_EMB;
    } else {
        return NULL;
    }
}

inline float* get_sum_vector(char* buf_out, int size_out, int idx) {
    if (idx < size_out / EMB_SIZE) {
        return (float*)buf_out + idx * DIM_EMB;
    } else {
        return NULL;
    }
}

int cxl_lookup(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    TableContext* tbl_ctx = (TableContext*)ctx->data;
    QueryContext* query_ctx = (QueryContext*)tbl_ctx->data;

    int out_idx = 0;
    for (int i = 0; i < tbl_ctx->n_table; i++) {
        int* offsets = get_offsets(query_ctx);
        int* indices = get_indices(query_ctx);
        
        for (int j = 0; j < query_ctx->n_query; j++) {
            int begin_offset = offsets[j];
            int end_offset = (j + 1 == query_ctx->n_query) ? query_ctx->n_indice : offsets[j + 1];
            float* sum = get_sum_vector(buf_out, size_out, out_idx);
            out_idx++;
            
            for (int k = begin_offset; k < end_offset; k++) {
                int idx = indices[k];
                float* vector = get_cxl_vector(&tables[i], idx);
                simd_add(sum, vector);
            }
        }
    }
    return 0;
}

size_t table_addr[10] = {0};


inline float* get_flash_vector(char* dram_buf, int n_page, int indice) {
    char* vector = dram_buf + (PAGE_SIZE * n_page) + PAGE_OFFSET(indice * EMB_SIZE);
    return (float*)vector;
}

int recssd_lookup(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    RecSSDContext* rec_ctx = (RecSSDContext*)ctx->data;
    TableContext* tbl_ctx = (TableContext*)rec_ctx->data;
    QueryContext* query_ctx = (QueryContext*)tbl_ctx->data;
    // should assert rec_ctx size >= ctx size
    if (rec_ctx->state == 0) {
        dlrm_debug("recssd_lookup: state 0\n");
        int n_input_embedding = 0;
        for (int i = 0; i < tbl_ctx->n_table; i++) {
            dlrm_debug("recssd_lookup: table %d\n", i);
            int* indices = query_ctx->data + query_ctx->n_query;
            for (int j = 0; j < query_ctx->n_indice; j++) {
                int page_offset = PAGE_ALIGN(indices[j] * EMB_SIZE);
                dlrm_debug("recssd_lookup: indice %d, page_offset %d\n", indices[j], page_offset);
                ctx->next_addr[n_input_embedding] = table_addr[i] + page_offset; // careful not to overflow
                ctx->size[n_input_embedding] = PAGE_SIZE;
                n_input_embedding++;
            }
        }
        dlrm_debug("n_input_embedding: %d\n", n_input_embedding);
        rec_ctx->state = tbl_ctx->n_table;
    } else {
        dlrm_debug("recssd_lookup: state 1\n");
        int n_input_embedding = 0;
        int out_idx = 0;
        for (int i = 0; i < tbl_ctx->n_table; i++) {
            dlrm_debug("recssd_lookup: table %d\n", i);
            int* offsets = get_offsets(query_ctx);
            int* indices = get_indices(query_ctx);
        
            for (int j = 0; j < query_ctx->n_query; j++) {
                dlrm_debug("recssd_lookup: query %d\n", j);
                int begin_offset = offsets[j];
                int end_offset = (j + 1 == query_ctx->n_query) ? query_ctx->n_indice : offsets[j + 1];
                float* sum = get_sum_vector(buf_out, size_out, out_idx);
                out_idx++;
                
                for (int k = begin_offset; k < end_offset; k++) {
                    dlrm_debug("recssd_lookup: indice %d\n", k);
                    int idx = indices[k];
                    float* vector = get_flash_vector((char*)buf_in, n_input_embedding, idx);
                    simd_add(sum, vector);
                    n_input_embedding++;
                }
            }
        }
        ctx->done = 1;
    }
    return 0;
}