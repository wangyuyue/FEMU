#include "nvme.h"
#include "test_function.h"

typedef struct Buffer {
    void* space;
    int size;
    int ref;
} Buffer;

void free_buf(Buffer* buf);

Buffer* alloc_buf(int size);

typedef struct ISC_Task {
    int task_id;
    int duration;
    int status;
    int blocking;

    QemuThread* thread;
    isc_function function;
    
    struct ISC_Task* upstream;
    Buffer* in_buf;
    Buffer* out_buf;
    
    NvmeRequest* req;
    NvmeComputeCmd cmd;
} ISC_Task;

enum {
    TASK_EMPTY     = 0,
    TASK_VALID     = 1,
    TASK_READY     = 2,
    TASK_BUSY      = 3,
    TASK_COMPLETED = 4,
};

void* worker(void* arg);

void* runtime(void* arg);

ISC_Task* get_free_task(void);

ISC_Task* get_task_by_id(int id);

void check_early_respond_req(FemuCtrl* n, NvmeRequest* req);

void clear_task(ISC_Task* task);

uint16_t buf_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req);

uint16_t buf_dma(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req, void* buf, int data_size);

// uint16_t nvme_rw2(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req);

#define runtime_log(fmt, ...) do { printf("[ISC RUNTIME] Log: " fmt, ## __VA_ARGS__); } while (0)