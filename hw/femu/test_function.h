typedef int (*isc_function)(void*, int, void*, int, void*);

int range_filter(void* buf_in, int size_in, void* buf_out, int size_out, void* arg);

int sum(void* buf_in, int size_in, void* buf_out, int size_out, void* arg);

int traverse(void* buf_in, int size_in, void* buf_out, int size_out, void* arg);