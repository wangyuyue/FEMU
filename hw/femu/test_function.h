typedef int (*isc_function)(void*, int, void*, int, void*);

int range_filter(void* buf_in, int size_in, void* buf_out, int size_out, void* arg);

int sum(void* buf_in, int size_in, void* buf_out, int size_out, void* arg);

int traverse(void* buf_in, int size_in, void* buf_out, int size_out, void* arg);

int kv_get(void* buf_in, int size_in, void* buf_out, int size_out, void* arg);

#define N_NEXT_ADDR 16

typedef struct tmp_ctx {
    int done;
	long next_addr[N_NEXT_ADDR];
	long size[N_NEXT_ADDR];
    char content[512];
} tmp_ctx;

void* content_addr(void* ctx);