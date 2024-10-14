#include "test_function.h"
#include "dlrm.h"
#include <assert.h>
#include <immintrin.h>
#include <stdio.h>
#include <string.h>

void init_flash_addr(DlrmShared* obj, int num_table, int num_emb[], size_t nvme_addr) {
    GenericArray* tables = malloc(sizeof(GenericArray) * num_table);
    void* addr = (void*)nvme_addr;
    for (int i = 0; i < num_table; i++) {
        tables[i].len = num_emb[i];
        tables[i].elem_size = EMB_SIZE;
        tables[i].data = addr;
        addr = last_addr(tables[i]);
    }
    obj->flash_tables = tables;
    obj->num_table = num_table;
}

void simd_add(float* sum, float* vector) {
    for (int l = 0; l < DIM_EMB; l += 8) { // Process 8 floats at a time
        __m256 vec_sum = _mm256_loadu_ps(&sum[l]); // Load sum[l] into AVX register
        __m256 vec_tbl = _mm256_loadu_ps(&vector[l]); // Load tbl[idx * DIM_EMB + l] into AVX register
        vec_sum = _mm256_add_ps(vec_sum, vec_tbl); // Perform vectorized sum
        _mm256_storeu_ps(&sum[l], vec_sum); // Store the result back to sum[l]
    }
}

inline float* get_sum_vector(char* buf_out, int size_out, int idx) {
    if (idx < size_out / EMB_SIZE) {
        return (float*)buf_out + idx * DIM_EMB;
    } else {
        return NULL;
    }
}

inline float* get_flash_vector(char* dram_buf, int n_page, int indice) {
    char* vector = dram_buf + (PAGE_SIZE * n_page) + PAGE_OFFSET(indice * EMB_SIZE);
    return (float*)vector;
}

// void prepare_resubmit(TaskContext* ctx, size_t table_addr, int n_indice, int* indices) {
void prepare_resubmit(TaskContext* ctx, DlrmPrivate* private_obj, DlrmShared* shared_obj, int table_id) {
    int n_indice = private_obj->req_params.indice_nr_list[table_id];
    int* indices = private_obj->req_params.indice_addr_list[table_id];
    size_t table_addr = (size_t)shared_obj->flash_tables[table_id].data;
    for (int i = 0; i < n_indice; i++) {
        int page_offset = PAGE_ALIGN(indices[i] * EMB_SIZE);
        ctx->next_addr[i] = table_addr + page_offset;
        ctx->size[i] = PAGE_SIZE;
    }
}

void* get_cached_vector(DlrmShared* dlrm_shared, int table_id, int indice) {
    return NULL;
}

inline void set_bitmap(char* bitmap, int seq_id) {
    bitmap[seq_id / 8] |= 1 << (seq_id % 8);
}

inline int get_bitmap(char* bitmap, int seq_id) {
    return bitmap[seq_id / 8] & (1 << (seq_id % 8));
}

inline void* get_page_vector(char* page, int indice) {
    return page + PAGE_OFFSET(indice * EMB_SIZE);
}

inline void* get_fetched_flash_vector(char* page, int seq_id, int indice, char* bitmap) {
    if (!get_bitmap(bitmap, seq_id))
        return NULL;
    return get_page_vector(page, indice);
}

int get_buf_in_page_nr(TaskContext* ctx, int i) {
    // return i;
    int* page_nr = ctx->private_obj;
    return page_nr[i];
}

void parse_recssd_ctx(RecSSDContext* rec_ctx, RequestParams* req_params) {
    int offset = 0;
    req_params->num_table = rec_ctx->data[0];
    offset++;
    for (int i = 0; i < req_params->num_table; i++) {
        req_params->offset_nr_list[i] = rec_ctx->data[offset++];
        req_params->indice_nr_list[i] = rec_ctx->data[offset++];
        req_params->offset_addr_list[i] = rec_ctx->data + offset;
        offset += req_params->offset_nr_list[i];
        req_params->indice_addr_list[i] = rec_ctx->data + offset;
        offset += req_params->indice_nr_list[i];
    }
}

void update_sum_vector(char* buf_in, char* buf_out, int size_out, DlrmPrivate* private_obj, int table_id) {
    int output_index = 0;
    int n_query = private_obj->req_params.offset_nr_list[table_id];
    int n_indice = private_obj->req_params.indice_nr_list[table_id];

    int* offsets = private_obj->req_params.offset_addr_list[table_id];
    int* indices = private_obj->req_params.indice_addr_list[table_id];
    for (int i = 0; i < n_indice; i++) {
        if (output_index < n_query && i == offsets[output_index + 1])
            output_index++;
        float* sum = get_sum_vector(buf_out, size_out, output_index);
        char* page = buf_in + i * PAGE_SIZE;
        float* vector = get_page_vector(page, indices[i]);
        simd_add(sum, vector);
    }
}

void* get_private_obj(TaskContext* ctx) {
    RecSSDContext* rec_ctx = (RecSSDContext*)ctx->data;
    if (rec_ctx->state != 0)
        return ctx->private_obj;

    DlrmPrivate* dlrm_private = malloc(sizeof(DlrmPrivate));
    parse_recssd_ctx(rec_ctx, &dlrm_private->req_params);
    for (int i = 0; i < dlrm_private->req_params.num_table; i++) {
        int len = dlrm_private->req_params.indice_nr_list[i] / 8 + 1;
        dlrm_private->bitmap_list[i] = malloc(len);
        memset(dlrm_private->bitmap_list[i], 0, len);
    }
    ctx->private_obj = dlrm_private;
    return dlrm_private;
}

int recssd_lookup(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    DlrmShared* dlrm_shared = ctx->shared_obj;

    RecSSDContext* rec_ctx = (RecSSDContext*)ctx->data;
    DlrmPrivate* dlrm_private = get_private_obj(ctx);
    RequestParams* params = &dlrm_private->req_params;

    // should assert rec_ctx size >= ctx size
    if (rec_ctx->state == 0) {
        dlrm_debug("recssd_lookup: state 0\n");
        for (int i = 0; i < params->num_table; i++) {
            dlrm_debug("recssd_lookup: table %d\n", i);
            prepare_resubmit(ctx, dlrm_private, dlrm_shared, i);
        }
        rec_ctx->state = 1;
    } else {
        dlrm_debug("recssd_lookup: state 1\n");
        for (int i = 0; i < params->num_table; i++) {
            dlrm_debug("recssd_lookup: table %d\n", i);
            update_sum_vector(buf_in, buf_out, size_out, dlrm_private, i);
        }
        ctx->done = 1;
    }
    return 0;
}