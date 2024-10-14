#ifndef __ISC_FUNCTION_H__
#define __ISC_FUNCTION_H__

typedef struct TaskContext TaskContext;

typedef int (*isc_function)(void*, int, void*, int, TaskContext*);

#define FUNCTION_DECLARATION(name) \
    int name(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* arg)

FUNCTION_DECLARATION(range_filter);
FUNCTION_DECLARATION(sum);
FUNCTION_DECLARATION(traverse);
FUNCTION_DECLARATION(kv_get);
FUNCTION_DECLARATION(kv_range);

#define N_NEXT_ADDR 512

typedef struct TaskContext {
    int done;
	long next_addr[N_NEXT_ADDR];
	long size[N_NEXT_ADDR];
    int len;
    int write_back;
    void* private_obj;
    void* shared_obj;
    char data[];
} TaskContext;
#endif