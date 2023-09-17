#include "isc_function.h"
#include "db_types.h"

isc_function function_list[] = {
    [0] = range_filter,
    [1] = sum,
    [2] = traverse,
    [3] = kv_get,
    [4] = kv_range,
};

int param_size[] = {
    [2] = 512,
    [3] = PARAM_SIZE(KVGetContext),
    [4] = PARAM_SIZE(KVRangeContext),
};

int app_context_size[] = {
    [2] = 512,
    [3] = sizeof(KVGetContext),
    [4] = sizeof(KVRangeContext),
};

int ret_val_size[] = {
    [3] = sizeof(KVGetRetVal),
    [4] = sizeof(KVRangeRetVal),
};

void free_buf(Buffer* buf) {
    buf->ref -= 1;
    if (buf->ref > 1) {
        runtime_log("ref count > 1\n");
        return;
    }
    g_free(buf->space);
    g_free(buf);
}

Buffer* alloc_buf(int size) {
    Buffer* buf = g_malloc(sizeof(Buffer));
    buf->space = g_malloc(size);
    memset(buf->space, 0, size);
    buf->size = size;
    buf->ref = 1;
    return buf;
}

static int cmp_pri(pqueue_pri_t next, pqueue_pri_t curr)
{
    return (next > curr);
}

static pqueue_pri_t get_pri(void *a)
{
    return ((NvmeRequest *)a)->expire_time;
}

static void set_pri(void *a, pqueue_pri_t pri)
{
    ((NvmeRequest *)a)->expire_time = pri;
}

static size_t get_pos(void *a)
{
    return ((NvmeRequest *)a)->pos;
}

static void set_pos(void *a, size_t pos)
{
    ((NvmeRequest *)a)->pos = pos;
}

#define N_TASK_ENTRY 64


ISC_Task task_list[N_TASK_ENTRY];

static int max_task_id = 0;

ISC_Task* get_free_task(void) {
    for (int i = 0; i < N_TASK_ENTRY; i++) {
        if (task_list[i].status == TASK_EMPTY) {
            task_list[i].task_id = max_task_id++;
            return &task_list[i];
        }
    }
    femu_err("no free task entry\n");
    return NULL;
}

ISC_Task* alloc_task(NvmeRequest* req) {
    ISC_Task* task = get_free_task();
    if (task == NULL) {
        return NULL;
    }
    task->req = req;
    memcpy(&(task->cmd), &(req->cmd), sizeof(NvmeComputeCmd));
    task->function = function_list[task->cmd.func_id];
    
    if (COMPUTE_FLAG(&req->cmd) & RESUBMIT) {
        task->context = alloc_buf(sizeof(TaskContext) + app_context_size[task->cmd.func_id]);
    }

    runtime_log("assign task %d status valid\n", task->task_id);
    task->status = TASK_VALID;
    req->isc_task_ptr = (uint64_t)task;

    return task;
}

ISC_Task* get_task_by_id(int id) {
    for (int i = 0; i < N_TASK_ENTRY; i++) {
        ISC_Task* task = &task_list[i];
        if (task->status != TASK_EMPTY && task->task_id == id)
            return task;
    }
    return NULL;
}

void clear_task(ISC_Task* task) {
    task->status = TASK_EMPTY;

    task->function = NULL;
    task->in_buf = NULL;
    task->out_buf = NULL;

    if (task->context)
        free_buf(task->context);
    task->context = NULL;

    task->upstream = NULL;

    task->req = NULL;
    memset(&(task->cmd), 0, sizeof(task->cmd));
    task->thread = NULL;
}

void check_early_respond_req(FemuCtrl* n, NvmeRequest* req) {
    ISC_Task* task = (ISC_Task*)(req->isc_task_ptr);
    if (task->cmd.compute_flag & BUF_TO_HOST) {
        task->blocking = 1;
    } else {
        task->blocking = 0;
        runtime_log("respond a unblocking task %d\n", task->task_id);
        req->cqe.res64 = task->task_id;
        
        enqueue_poller(n, req);
    }
    return;
}

static void record_time(char op_c) {
#if 0
    static int n_record = 0;
    static uint64_t record_time[1000];
    switch (op_c)
    {
    case 'r':
        record_time[n_record++] = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        break;
    case 'p':
        printf("time elapse:\n");
        for (int i = 1; i < n_record; i ++) {
            uint64_t diff = record_time[i] - record_time[i-1];
            printf("[%ld us, %ld ns]\n", diff / 1000, diff % 1000);
        }
        n_record = 0;
        break;
    default:
        printf("Unsupported latency record op: %c\n", op_c);
        exit(-1);
    }
#else
    return;
#endif
}

void* worker(void* arg) {
    ISC_Task* task = (ISC_Task*)arg;
    runtime_log("task %d begin working...\n", task->task_id);
    isc_function func = task->function;
    char* context_space = NULL;
    if (task->context)
        context_space = task->context->space;

    record_time('r');

    func(task->in_buf->space, task->in_buf->size, task->out_buf->space, task->out_buf->size, context_space);

    record_time('r');
    
    runtime_log("assign task %d status completed\n", task->task_id);
    task->status = TASK_COMPLETED;
    return 0;
}

NvmeRequest* dequeue_comp_req(FemuCtrl* n) {
    NvmeRequest* req = NULL;
    if (femu_ring_count(n->comp_req_queue)) {
        int rc = femu_ring_dequeue(n->comp_req_queue, (void *)&req, 1);
        if (rc != 1) {
            femu_err("dequeue from comp_req_queue request failed\n");
        }
        assert(req);
    }
    return req;
}

void enqueue_ftl(FemuCtrl* n, NvmeRequest* req) {
    int poller_id = n->multipoller_enabled ? req->sq->sqid : 1;
    runtime_log("get a compute req from poller %d, send to ftl\n", poller_id);
    int rc = femu_ring_enqueue(n->to_ftl[poller_id], (void *)&req, 1);
    if (rc != 1) {
        femu_err("enqueue to ftl request failed\n");
    }
}

void enqueue_poller(FemuCtrl* n, NvmeRequest* req) {
    int poller_id = n->multipoller_enabled ? req->sq->sqid : 1;
    runtime_log("finish a compute req, send to poller %d\n", poller_id);
    int rc = femu_ring_enqueue(n->to_poller[poller_id], (void *)&req, 1);
    if (rc != 1) {
        femu_err("enqueue to_poller request failed\n");
    }
}

void update_backend_io_timing(FemuCtrl* n) {
    while (femu_ring_count(n->to_runtime)) {
        NvmeRequest* req = NULL;
        int rc = femu_ring_dequeue(n->to_runtime, (void *)&req, 1);
        if (rc != 1) {
            femu_err("dequeue from to_runtime request failed\n");
        }
        assert(req);
        pqueue_insert(n->runtime_pq, req);
    }
}

void postprocess_backend_io(FemuCtrl* n, NvmeRequest* req) {
    check_early_respond_req(n, req);
        
    ISC_Task* task = (ISC_Task*)(req->isc_task_ptr);
    
    runtime_log("assert for task %d\n", task->task_id);
    assert(task->status == TASK_VALID);
    runtime_log("assign task %d status ready\n", task->task_id);
    
    task->status = TASK_READY;
}

void check_compl_backend_io(FemuCtrl* n) {
    NvmeRequest* req;
    while ((req = pqueue_peek(n->runtime_pq))) {
        uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        if (now < req->expire_time) {
            break;
        }
        runtime_log("compute req complete read flash\n");
        pqueue_pop(n->runtime_pq);
        
        postprocess_backend_io(n, req);
    }
}

void check_task_ready(ISC_Task* task) {
    if (task->status == TASK_VALID && task->upstream) {
        if (task->upstream->status == TASK_COMPLETED) {
            runtime_log("assign task %d status ready\n", task->task_id);
            task->status = TASK_READY;
        }
    }
}

void launch_task(ISC_Task* task) {
    runtime_log("assign task %d status busy\n", task->task_id);
    
    task->status = TASK_BUSY;
    // task->thread = malloc(sizeof(QemuThread));
    
    worker(task);
    // qemu_thread_create(task->thread, "isc-worker", worker, task, QEMU_THREAD_JOINABLE);
}

void postprocess_task(FemuCtrl* n, ISC_Task* task) {
    if (task->thread) {
        // qemu_thread_join(task->thread);
        free(task->thread);
        task->thread = NULL;
    }

    if (task->blocking) {
        runtime_log("get a completed blocking task %d\n", task->task_id);

        NvmeRequest* req = task->req;

        if (COMPUTE_FLAG(&req->cmd) & RESUBMIT) {
            runtime_log("it is a resubmit compute request\n");
            TaskContext* ctx = task->context->space;
            if (ctx->done == 0) {
                uint64_t next_blk = ctx->next_addr[0];
                NvmeComputeCmd* comp = (NvmeComputeCmd*)&req->cmd;
                comp->slba = next_blk / 512;
                task->status = TASK_VALID;
                runtime_log("next block %p, resubmit\n", (void*)next_blk);
                buf_rw(n, req);
                
                req->expire_time = req->stime = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
                enqueue_ftl(n, req);
                return;
            }
        }
        int is_write = 0;
        
        buf_dma(n, req, task->out_buf->space, task->out_buf->size, is_write);

        record_time('r');

        req->cqe.res64 = task->task_id;
        enqueue_poller(n, req);

        runtime_log("assign task %d status empty\n", task->task_id);
        clear_task(task);

        record_time('p');
    }
}

void* runtime(void* arg) {
    FemuCtrl* n = (FemuCtrl*)arg;
    // n->workers = malloc(sizeof(QemuThread) * n->n_worker);
    // for (int i = 0; i < n->n_worker; i++) {
    //     qemu_thread_create(&n->workers[i], "isc-worker", worker_thread, NULL);
    // }
    runtime_log("runtime starts running\n");
    while(1) {
        NvmeRequest* req = dequeue_comp_req(n);
        if (req) {
            ISC_Task* task = alloc_task(req);

            if (COMPUTE_FLAG(&req->cmd) & HAS_CONTEXT) {
                int is_write = 1;
                buf_dma(n, req, APP_CONTEXT(task->context->space), param_size[task->cmd.func_id], is_write);
            }

            int compute_flag = COMPUTE_FLAG(&req->cmd);
            assert((compute_flag & WRITE_FLASH) == 0);
            assert((compute_flag & HOST_TO_BUF) == 0);
            if (compute_flag & READ_FLASH) {
                buf_rw(n, req);
                enqueue_ftl(n, req);
            } else {
                int upstream_id = task->cmd.upstream_id;
                ISC_Task* upstream_task = get_task_by_id(upstream_id);
                assert(upstream_task != NULL);
                task->upstream = upstream_task;
                
                task->in_buf = upstream_task->out_buf;
                task->in_buf->ref += 1;

                int out_buf_size = (ret_val_size[task->cmd.func_id] == 0) ? \
                                task->in_buf->size : ret_val_size[task->cmd.func_id];
 
                task->out_buf = alloc_buf(out_buf_size);
                check_early_respond_req(n, req);
            }
        }

        update_backend_io_timing(n);
        check_compl_backend_io(n);

        for (int i = 0; i < N_TASK_ENTRY; i++) {
            ISC_Task* task = &task_list[i];
            check_task_ready(task);
            if (task->status == TASK_READY) {
                launch_task(task);
            }
        }

        for (int i = 0; i < N_TASK_ENTRY; i++) {
            ISC_Task* task = &task_list[i];
            if (task->status == TASK_COMPLETED) {
                postprocess_task(n, task);
            }
        }
    }
}

void init_runtime(FemuCtrl* n) {
    runtime_log("start init runtime\n");
    n->n_worker = 1;
    n->to_runtime = femu_ring_create(FEMU_RING_TYPE_MP_SC, FEMU_MAX_INF_REQS);
    n->comp_req_queue = femu_ring_create(FEMU_RING_TYPE_MP_SC, FEMU_MAX_INF_REQS);

    n->runtime_pq = pqueue_init(FEMU_MAX_INF_REQS, cmp_pri, get_pri, set_pri,
                                get_pos, set_pos);
    if (!n->runtime_pq) {
        femu_err("fail to create pqueue for isc runtime\n");
        abort();
    }

    qemu_thread_create(&n->runtime, "FEMU-RUNTIME", runtime, (void*)n, QEMU_THREAD_JOINABLE);
    runtime_log("finish init runtime\n");
}

uint16_t buf_rw(FemuCtrl *n, NvmeRequest *req)
{
    NvmeNamespace* ns = req->ns;

    NvmeComputeCmd *comp = (NvmeComputeCmd*)&(req->cmd);
    uint16_t ctrl = le16_to_cpu(comp->control);
    uint32_t nlb  = le16_to_cpu(comp->nlb) + 1;
    uint64_t slba = le64_to_cpu(comp->slba);

    const uint8_t lba_index = NVME_ID_NS_FLBAS_INDEX(ns->id_ns.flbas);
    const uint16_t ms = le16_to_cpu(ns->id_ns.lbaf[lba_index].ms);
    const uint8_t data_shift = ns->id_ns.lbaf[lba_index].lbads;
    uint64_t data_size = (uint64_t)nlb << data_shift;
    uint64_t data_offset = slba << data_shift;
    uint64_t meta_size = nlb * ms;
    uint64_t elba = slba + nlb;
    uint16_t err;
    runtime_log("slba: %p, data size %ld\n", (void*)slba, data_size);

    req->is_write = 0;

    err = femu_nvme_rw_check_req(n, ns, (NvmeCmd*)comp, req, slba, elba, nlb, ctrl,
                                 data_size, meta_size);
    if (err)
        return err;

    req->slba = slba;
    req->status = NVME_SUCCESS;
    req->nlb = nlb;

    ISC_Task* task = (ISC_Task*)req->isc_task_ptr;
    // previous buf for a resubmit compute request can be reused here
    if (task->in_buf == NULL)
        task->in_buf = alloc_buf(data_size);
    void* buf = task->in_buf->space;
    
    if (task->out_buf == NULL) {
        int out_buf_size = (ret_val_size[task->cmd.func_id] == 0) ? \
                        task->in_buf->size : ret_val_size[task->cmd.func_id];
 
        task->out_buf = alloc_buf(out_buf_size);
    }

    SsdDramBackend *b = n->mbe;
    void* mb = b->logical_space;
    if (b->femu_mode != FEMU_BBSSD_MODE) {
        femu_err("buf_rw only support black-box SSD");
    }
    if (req->is_write) {
        memcpy(mb + data_offset, buf, data_size);
    } else {
        memcpy(buf, mb + data_offset, data_size);
    }

    return 0;
}

uint16_t buf_dma(FemuCtrl *n, NvmeRequest *req, void* buf, int data_size, int is_write) {
    NvmeComputeCmd *comp = (NvmeComputeCmd *)&(req->cmd);

    uint64_t prp1 = le64_to_cpu(comp->prp1);
    uint64_t prp2 = le64_to_cpu(comp->prp2);
    runtime_log("prp1: %ld, prp2: %ld, data size %d\n", prp1, prp2, data_size);

    if (nvme_map_prp(&req->qsg, &req->iov, prp1, prp2, data_size, n)) {
        nvme_set_error_page(n, req->sq->sqid, comp->cid, NVME_INVALID_FIELD,
                            offsetof(NvmeRwCmd, prp1), 0, req->ns->id);
        femu_err("nvme map prp error\n");
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    req->status = NVME_SUCCESS;

    uint64_t data_offset = 0;

    QEMUSGList * qsg = &req->qsg;
    int sg_cur_index = 0;
    dma_addr_t sg_cur_byte = 0;
    dma_addr_t cur_addr, cur_len;

    DMADirection dir = DMA_DIRECTION_FROM_DEVICE;

    if (is_write) {
        dir = DMA_DIRECTION_TO_DEVICE;
    }

    while (sg_cur_index < qsg->nsg) {
        cur_addr = qsg->sg[sg_cur_index].base + sg_cur_byte;
        cur_len = qsg->sg[sg_cur_index].len - sg_cur_byte;
        if (dma_memory_rw(qsg->as, cur_addr, buf + data_offset, cur_len, dir, MEMTXATTRS_UNSPECIFIED)) {
            femu_err("dma_memory_rw error");
        }
        sg_cur_byte += cur_len;
        if (sg_cur_byte == qsg->sg[sg_cur_index].len) {
            sg_cur_byte = 0;
            ++sg_cur_index;
        }

        data_offset += cur_len;
    }
    qemu_sglist_destroy(qsg);
    
    return NVME_SUCCESS;
}
