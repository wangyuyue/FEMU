#include "nvme.h"
#include "test_function.h"

typedef struct Buffer {
    void* space;
    int size;
    int ref;
} Buffer;

void free_buf(Buffer* buf);

Buffer* alloc_buf(int size);

typedef enum {
    ISC_READ_HOST = 0,
    ISC_WRITE_HOST = 1,
    ISC_READ_FLASH = 2,
    ISC_WRITE_FLASH = 3,
} isc_dma_direction;

typedef struct ISC_Task {
    int task_id;
    uint64_t duration;
    int status;
    int blocking;

    QemuThread* thread;
    isc_function function;
    
    struct ISC_Task* upstream;
    Buffer* in_buf;
    Buffer* out_buf;
    Buffer* context;

    Buffer* dma_buf;
    isc_dma_direction dir;
    
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

#define COMPUTE_FLAG(cmd) (((NvmeComputeCmd*)cmd)->compute_flag)

void* worker(void* arg);

void* runtime(void* arg);

ISC_Task* get_free_task(void);

ISC_Task* alloc_task(NvmeRequest* req);

ISC_Task* get_task_by_id(int id);

void clear_task(ISC_Task* task);

void check_early_respond_req(FemuCtrl* n, NvmeRequest* req);

NvmeRequest* dequeue_comp_req(FemuCtrl* n);

void enqueue_ftl(FemuCtrl* n, NvmeRequest* req);

void enqueue_poller(FemuCtrl* n, NvmeRequest* req);

void update_backend_io_timing(FemuCtrl* n);

void postprocess_backend_io(FemuCtrl* n, NvmeRequest* req);

void check_compl_backend_io(FemuCtrl* n);

void check_task_ready(ISC_Task* task);

void launch_task(ISC_Task* task);

void postprocess_task(FemuCtrl* n, ISC_Task* task);

uint16_t buf_rw(FemuCtrl *n, NvmeRequest *req);

uint16_t buf_dma(FemuCtrl *n, NvmeRequest *req, void* buf, int data_size, int is_write);

#define PARAM_SIZE(TYPE) (sizeof(((TYPE*)0)->params))

// uint16_t nvme_rw2(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req);

#if 1
#define runtime_log(fmt, ...) do { printf("[ISC RUNTIME] Log: " fmt, ## __VA_ARGS__); } while (0)
#else
#define runtime_log(fmt, ...) do { } while (0)
#endif