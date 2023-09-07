typedef int (*isc_function)(void*, int, void*, int, void*);

#define FUNCTION_DECLARATION(name) \
    int name(void* buf_in, int size_in, void* buf_out, int size_out, void* arg)

FUNCTION_DECLARATION(range_filter);
FUNCTION_DECLARATION(sum);
FUNCTION_DECLARATION(traverse);
FUNCTION_DECLARATION(kv_get);
FUNCTION_DECLARATION(kv_range);

#define N_NEXT_ADDR 16

typedef struct TaskContext {
    int done;
	long next_addr[N_NEXT_ADDR];
	long size[N_NEXT_ADDR];
} TaskContext;

#define APP_CONTEXT(context_ptr) ((char*)context_ptr + sizeof(TaskContext))