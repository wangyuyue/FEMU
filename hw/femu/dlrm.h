#include <stddef.h>

typedef struct {
    int len;
    int elem_size;
    void* data;
} GenericArray;

inline void* last_addr(GenericArray arr) {
    return (char*)arr.data + arr.len * arr.elem_size;
}

typedef struct {
    int state;
    int data[];
} RecSSDContext;

#define MAX_TABLE_NUM 16
typedef struct {
    int num_table;
    int offset_nr_list[MAX_TABLE_NUM];
    int indice_nr_list[MAX_TABLE_NUM];
    int* offset_addr_list[MAX_TABLE_NUM];
    int* indice_addr_list[MAX_TABLE_NUM];
} RequestParams;

typedef struct {
    RequestParams req_params;
    char* bitmap_list[MAX_TABLE_NUM];
} DlrmPrivate;

void parse_recssd_ctx(RecSSDContext* rec_ctx, RequestParams* req_params);

extern int dim_vec;

#define DIM_EMB 128
#define EMB_SIZE (DIM_EMB * sizeof(float))

#define PAGE_SIZE 512
#define PAGE_ALIGN(x) ((x) & ~(PAGE_SIZE - 1))
#define PAGE_OFFSET(x) ((x) & (PAGE_SIZE - 1))

typedef struct {
    GenericArray cxl_cache;
    GenericArray ftl_cache;
    int num_table;
    GenericArray* flash_tables;
} DlrmShared;

void init_flash_addr(DlrmShared* obj, int n_table, int table_size[], size_t nvme_addr);

void* get_private_obj(TaskContext* ctx);

void* get_cached_vector(DlrmShared* dlrm_shared, int table_id, int indice);

void prepare_resubmit(TaskContext* ctx, DlrmPrivate* private_obj, DlrmShared* shared_obj, int table_id);

int get_buf_in_page_nr(TaskContext* ctx, int i);

void update_sum_vector(char* buf_in, char* buf_out, int size_out, DlrmPrivate* private_obj, int table_id);

int recssd_lookup(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx);

void simd_add(float* sum, float* vector);

#if 0
#define dlrm_debug(fmt, ...) do { fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)
#else
#define dlrm_debug(fmt, ...) do { } while (0)
#endif