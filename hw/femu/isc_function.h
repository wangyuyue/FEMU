#include "nvme.h"

typedef int (*isc_function)(void*, void*, int, int ,int, int);

typedef struct Buffer {
    void* space;
    int size;
    int ref;
} Buffer;

void free_buf(Buffer* buf);

Buffer* alloc_buf(int size);

typedef struct ISC_Task {
    QemuThread* thread;
    isc_function function;
    Buffer* in_buf;
    Buffer* out_buf;
    int duration;
    int task_id;
    int valid;
    int busy;
    int completed;
} ISC_Task;

void* worker(void* arg);

void* runtime(void* arg);

uint16_t buf_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req);

uint16_t buf_dma(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req, void* buf, int data_size);

// uint16_t nvme_rw2(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req);

#define runtime_log(fmt, ...) do { printf("[ISC RUNTIME] Log: " fmt, ## __VA_ARGS__); } while (0)