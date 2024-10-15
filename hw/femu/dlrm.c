#include "test_function.h"
#include "dlrm.h"
#include <assert.h>
#include <immintrin.h>
#include <stdio.h>
#include <string.h>

void init_flash_addr(DlrmShared* obj, int num_table, int num_emb[], size_t nvme_addr) {
    Array2D* tables = malloc(sizeof(Array2D) * num_table);
    void* addr = (void*)nvme_addr;
    for (int i = 0; i < num_table; i++) {
        tables[i].dim_o = num_emb[i];
        tables[i].dim_i = DIM_EMB;
        tables[i].elem_size = sizeof(float);
        tables[i].data = addr;
        addr = array_tail_2d(tables[i]);
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

inline float* get_flash_vector(char* dram_buf, int n_page, int indice) {
    char* vector = dram_buf + (PAGE_SIZE * n_page) + PAGE_OFFSET(indice * EMB_SIZE);
    return (float*)vector;
}

inline void set_bitmap(char* bitmap, int seq_id) {
    bitmap[seq_id / 8] |= 1 << (seq_id % 8);
}

inline int get_bitmap(char* bitmap, int seq_id) {
    return bitmap[seq_id / 8] & (1 << (seq_id % 8));
}

void prepare_resubmit(TaskContext* ctx, DlrmPrivate* private_obj, DlrmShared* shared_obj, int table_id) {
    int n_indice = private_obj->req_params.indice_nr_list[table_id];
    int* indices = private_obj->req_params.indice_addr_list[table_id];
    size_t table_addr = (size_t)shared_obj->flash_tables[table_id].data;
    int n_miss = 0;
    for (int i = 0; i < n_indice; i++) {
        float* vector = get_cached_vector(shared_obj, table_id, indices[i]);
        if (vector != NULL) {
            continue;
        }
        // set_bitmap(private_obj->bitmap_list[table_id], i);
        ctx->next_addr[n_miss] = table_addr + PAGE_ALIGN(indices[i] * EMB_SIZE);
        ctx->size[n_miss] = PAGE_SIZE;
        n_miss++;
    }
}

void* get_cached_vector(DlrmShared* dlrm_shared, int table_id, int indice) {
    return NULL;
}

inline Array1D get_page_vector(char* page, int indice) {
    return (Array1D){DIM_EMB, sizeof(float), page + PAGE_OFFSET(indice * EMB_SIZE)};
}

inline int size_2d_array(Array2D arr) {
    return arr.dim_o * arr.dim_i * arr.elem_size;
}

void init_result_bufs(char* buf_out , int size_out, DlrmPrivate* dlrm_private) {
    Array2D* result_bufs = dlrm_private->result_bufs;
    int offset = 0;
    for (int i = 0; i < dlrm_private->req_params.num_table; i++) {
        result_bufs[i].dim_o = dlrm_private->req_params.offset_nr_list[i];
        result_bufs[i].dim_i = DIM_EMB;
        result_bufs[i].elem_size = sizeof(float);
        result_bufs[i].data = malloc(size_2d_array(result_bufs[i]));
        printf("result buf space: %p\n", result_bufs[i].data);
        // result_bufs[i].data = buf_out + offset;
        offset += size_2d_array(result_bufs[i]);
    }
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

void update_sum_vector(char* buf_in, DlrmPrivate* private_obj, int table_id) {
    printf("update_sum_vector\n");
    Array2D result_buf = private_obj->result_bufs[table_id];
    int output_index = 0;
    RequestParams* req_params = &private_obj->req_params;
    int n_query = req_params->offset_nr_list[table_id];
    int n_indice = req_params->indice_nr_list[table_id];

    int* offsets = req_params->offset_addr_list[table_id];
    int* indices = req_params->indice_addr_list[table_id];
    int n_miss = 0;
    Array1D sum_vec = at_ith_2d(result_buf, output_index);
    for (int i = 0; i < n_indice; i++) {
        if (output_index + 1 < n_query && i == offsets[output_index + 1]) {
            output_index++;
            sum_vec = at_ith_2d(result_buf, output_index);
        }
        // if (get_bitmap(private_obj->bitmap_list[table_id], i)) {
            char* page = buf_in + n_miss * PAGE_SIZE;
            Array1D vector = get_page_vector(page, indices[i]);
            n_miss++;
            simd_add(sum_vec.data, vector.data);
            printf("sumvec buf space: %p\n", sum_vec.data);
        // }
    }
    printf("update_sum_vector done\n");
}

void* get_private_obj(TaskContext* ctx) {
    RecSSDContext* rec_ctx = (RecSSDContext*)ctx->data;
    if (rec_ctx->state != 0)
        return ctx->private_obj;

    DlrmPrivate* dlrm_private = malloc(sizeof(DlrmPrivate));
    printf("dlrm_private buf space: %p\n", dlrm_private);
    parse_recssd_ctx(rec_ctx, &dlrm_private->req_params);
    // for (int i = 0; i < dlrm_private->req_params.num_table; i++) {
        // int len = dlrm_private->req_params.indice_nr_list[i] / 8 + 1;
        // dlrm_private->bitmap_list[i] = malloc(len);
        // memset(dlrm_private->bitmap_list[i], 0, len);
    // }
    ctx->private_obj = dlrm_private;
    return dlrm_private;
}

int recssd_lookup(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    printf("out_size: %d\n", size_out);
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
        init_result_bufs(buf_out, size_out, dlrm_private);
        int out_offset = 0;
        for (int i = 0; i < params->num_table; i++) {
            dlrm_debug("recssd_lookup: table %d\n", i);
            update_sum_vector(buf_in, dlrm_private, i);
            int copy_size = size_2d_array(dlrm_private->result_bufs[i]);
            memcpy(buf_out + out_offset, dlrm_private->result_bufs[i].data, copy_size);
            out_offset += copy_size;
        }
        // assert(out_offset <= size_out);
        ctx->done = 1;
    }
    return 0;
}