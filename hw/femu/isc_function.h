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
} ISC_DMA_Direction;

typedef struct DMA_Vec_Entry {
    void* buf;
    uint64_t offset;
    uint32_t size;
    ISC_DMA_Direction dir;
} DMA_Vec_Entry;

typedef struct DMA_Vec {
    int nvec;
    int total_size;
    DMA_Vec_Entry* vec; // Pointer to an array of DMA_Vec_Entry structures
} DMA_Vec;

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
    Buffer* context_buf;

    DMA_Vec dma_vec;
    
    NvmeRequest* req;
    NvmeComputeCmd cmd;
} ISC_Task;

TaskContext* get_ctx(ISC_Task* task);

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

void enqueue_ftl(FemuCtrl* n, ISC_Task* task);

void enqueue_poller(FemuCtrl* n, NvmeRequest* req);

void update_backend_io_timing(FemuCtrl* n);

void postprocess_backend_io(FemuCtrl* n, NvmeRequest* req);

void check_compl_backend_io(FemuCtrl* n);

void check_task_ready(ISC_Task* task);

void launch_task(ISC_Task* task);

void compl_task(FemuCtrl* n, ISC_Task* task);

void postprocess_task(FemuCtrl* n, ISC_Task* task);

uint16_t flash_dma(FemuCtrl *n, ISC_Task *task);

void set_dma_vec_for_req(FemuCtrl *n, NvmeRequest* req);

void set_dma_vec_for_task(FemuCtrl* n, ISC_Task* task);

void alloc_task_buf(ISC_Task* task);

uint16_t host_dma(FemuCtrl *n, NvmeRequest *req, void* buf, int data_size, int is_write);

#define PARAM_SIZE(TYPE) (sizeof(((TYPE*)0)->params))

// uint16_t nvme_rw2(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req);

#if 0
#define runtime_log(fmt, ...) do { printf("[ISC RUNTIME] Log: " fmt, ## __VA_ARGS__); } while (0)
#else
#define runtime_log(fmt, ...) do { } while (0)
#endif