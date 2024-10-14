#include "isc_function.h"
#include "db_types.h"
#include "isc_cc.h"
#include "dlrm.h"

TaskContext* get_ctx(ISC_Task* task) {
    return (TaskContext*)(task->context_buf->space);
}

DlrmShared dlrm_shared;

isc_function function_list[] = {
    [0] = range_filter,
    [1] = sum,
    [2] = traverse,
    [3] = kv_get,
    [4] = kv_range,
    [5] = print_bucket,
    [6] = bighash_lookup,
    [7] = bighash_remove,
    [8] = recssd_lookup,
};

int param_size[] = {
    [2] = 512,
    [3] = PARAM_SIZE(KVGetContext),
    [4] = PARAM_SIZE(KVRangeContext),
    [6] = 512,
    [7] = 512,
    [8] = 4096,
};

int app_context_size[] = {
    [2] = 512,
    [3] = sizeof(KVGetContext),
    [4] = sizeof(KVRangeContext),
    [6] = 512,
    [7] = 512,
    [8] = 4096,
};

int ret_val_size[] = {
    [3] = sizeof(KVGetRetVal),
    [4] = sizeof(KVRangeRetVal),
    [6] = 4096,
    [7] = 4096,
    [8] = 4096, // big enough to hold aggregated embedding vectors
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
    
    size_t app_ctx_size = 0;
    if (COMPUTE_FLAG(&req->cmd) & HAS_CONTEXT) {
        app_ctx_size = app_context_size[task->cmd.func_id];
        assert(app_ctx_size > 0);   
    }
    task->context_buf = alloc_buf(sizeof(TaskContext) + app_ctx_size);
    get_ctx(task)->len = app_ctx_size;

    runtime_log("assign task %d status valid\n", task->task_id);
    task->status = TASK_VALID;
    req->isc_task_ptr = task;

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

    if (task->context_buf)
        free_buf(task->context_buf);
    task->context_buf = NULL;

    task->upstream = NULL;

    task->req = NULL;
    memset(&(task->cmd), 0, sizeof(task->cmd));
    task->thread = NULL;
}

void check_early_respond_req(FemuCtrl* n, NvmeRequest* req) {
    ISC_Task* task = req->isc_task_ptr;
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
    ISC_Task* task = arg;
    runtime_log("task %d begin working...\n", task->task_id);
    isc_function func = task->function;
    TaskContext* cxt = NULL;
    if (task->context_buf) {
        runtime_log("task %d has context\n", task->task_id);
        cxt = get_ctx(task);
    }

    record_time('r');

    void* buf_in = task->in_buf ? task->in_buf->space : NULL;
    int in_size = task->in_buf ? task->in_buf->size : 0;
    void* buf_out = task->out_buf ? task->out_buf->space : NULL;
    int out_size = task->out_buf ? task->out_buf->size : 0;
    func(buf_in, in_size, buf_out, out_size, cxt);

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

void enqueue_ftl(FemuCtrl* n, ISC_Task* task) {
    runtime_log("get a compute req, send to ftl\n");
    int rc = femu_ring_enqueue(n->from_runtime, (void *)&task, 1);
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
        ISC_Task* task = NULL;
        int rc = femu_ring_dequeue(n->to_runtime, (void *)&task, 1);
        if (rc != 1) {
            femu_err("dequeue from to_runtime request failed\n");
        }
        assert(task->req);
        pqueue_insert(n->runtime_pq, task->req);
    }
}

void postprocess_backend_io(FemuCtrl* n, NvmeRequest* req) {
    check_early_respond_req(n, req);
        
    ISC_Task* task = req->isc_task_ptr;
    assert(task->status == TASK_VALID);
    
    if (get_ctx(task)->write_back) {
        compl_task(n, task);
        return;
    }
    runtime_log("assign task %d ready\n", task->task_id);
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

void compl_task(FemuCtrl* n, ISC_Task* task) {
    NvmeRequest* req = task->req;
    req->cqe.res64 = task->task_id;
    enqueue_poller(n, req);

    runtime_log("assign task %d status empty\n", task->task_id);
    clear_task(task);
}

void postprocess_task(FemuCtrl* n, ISC_Task* task) {
    runtime_log("postprocess task %d\n", task->task_id);
    if (task->thread) {
        // qemu_thread_join(task->thread);
        free(task->thread);
        task->thread = NULL;
    }
    TaskContext* ctx = get_ctx(task);
    runtime_log("blocking: %d\n", task->blocking);
    if (task->blocking) {
        runtime_log("get a completed blocking task %d\n", task->task_id);

        NvmeRequest* req = task->req;

        if (COMPUTE_FLAG(&req->cmd) & RESUBMIT) {
            runtime_log("it is a resubmit compute request\n");
            if (ctx->done == 0) {
                set_dma_vec_for_task(n, task);
                task->status = TASK_VALID;
                flash_dma(n, task);
                enqueue_ftl(n, task);
                return;
            } else {
                // printf("task %d done\n", task->task_id);
            }
        }
        if (ctx->write_back) {
            task->dma_vec.vec[0].dir = ISC_WRITE_FLASH;
            task->status = TASK_VALID;
            runtime_log("write back to flash\n");
            flash_dma(n, task);
            enqueue_ftl(n, task);
        }

        int is_write = 0;
        
        host_dma(n, req, task->out_buf->space, task->out_buf->size, is_write);
        if (ctx->write_back)
            return;

        record_time('r');

        compl_task(n, task);

        record_time('p');
    }
}

void* runtime(void* arg) {
    FemuCtrl* n = arg;
    // n->workers = malloc(sizeof(QemuThread) * n->n_worker);
    // for (int i = 0; i < n->n_worker; i++) {
    //     qemu_thread_create(&n->workers[i], "isc-worker", worker_thread, NULL);
    // }
    runtime_log("runtime starts running\n");
    init_flash_addr(&dlrm_shared, 1, (int[]){10000}, 0x0);
    while(1) {
        NvmeRequest* req = dequeue_comp_req(n);
        if (req) {
            ISC_Task* task = alloc_task(req);

            if (COMPUTE_FLAG(&req->cmd) & HAS_CONTEXT) {
                int is_write = 1;
                runtime_log("compute req has context, write context to device memory\n");
                host_dma(n, req, get_ctx(task)->data, param_size[task->cmd.func_id], is_write);
                runtime_log("finish device memory write\n");
            }

            int compute_flag = COMPUTE_FLAG(&req->cmd);
            assert((compute_flag & WRITE_FLASH) == 0);
            assert((compute_flag & HOST_TO_BUF) == 0);
            if (compute_flag & READ_FLASH) {
                set_dma_vec_for_req(n, req);
                if (task->dma_vec.nvec == 0) {
                    runtime_log("read flash dma failed\n");
                    enqueue_poller(n, req);
                    clear_task(task);
                    continue;
                }
                alloc_task_buf(task);
                flash_dma(n, task);
                enqueue_ftl(n, task);
                record_time('r');
            } else if (task->function == function_list[8]) {
                get_ctx(task)->shared_obj = &dlrm_shared;
                task->status = TASK_READY;
                check_early_respond_req(n, req);
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
    n->from_runtime = femu_ring_create(FEMU_RING_TYPE_SP_SC, FEMU_MAX_ISC_TASKS);
    n->to_runtime = femu_ring_create(FEMU_RING_TYPE_SP_SC, FEMU_MAX_ISC_TASKS);
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

uint16_t flash_dma(FemuCtrl *n, ISC_Task* task) {
    runtime_log("flash_dma\n");
    SsdDramBackend *b = n->mbe;
    void* mb = b->logical_space;
    if (b->femu_mode != FEMU_BBSSD_MODE) {
        femu_err("flash_dma only support black-box SSD\n");
        abort();
    }
    DMA_Vec vec = task->dma_vec;
    for (int i = 0; i < vec.nvec; i++) {
        DMA_Vec_Entry* entry = &vec.vec[i];
        int data_size = entry->size;
        int flash_offset = entry->offset;
        runtime_log("flash_offset: %d, data_size: %d\n", flash_offset, data_size);
        if(entry->buf == NULL) {
            femu_err("flash_dma buf is NULL\n");
            return 1;
        }
        switch (entry->dir) {
            case ISC_READ_FLASH:
                memcpy(entry->buf, mb + flash_offset, data_size);
                break;
            case ISC_WRITE_FLASH:
                memcpy(mb + flash_offset, entry->buf, data_size);
                break;
            default:
                femu_err("Unsupported flash_dma direction\n");
        }
    }
    return 0;
}

void set_dma_vec_for_req(FemuCtrl* n, NvmeRequest* req) {
    ISC_Task* task = req->isc_task_ptr;

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
    if (err) {
        task->dma_vec = (DMA_Vec){0};
        return;
    }

    req->slba = slba;
    req->status = NVME_SUCCESS;
    req->nlb = nlb;
    task->dma_vec = (DMA_Vec){1, data_size, g_malloc(sizeof(DMA_Vec_Entry))};
    task->dma_vec.vec[0] = (DMA_Vec_Entry){NULL, data_offset, data_size, ISC_READ_FLASH};
    return;
}

void set_dma_vec_for_task(FemuCtrl* n, ISC_Task* task) {
    TaskContext* ctx = get_ctx(task);
    int n_vec = 0;
    int total_size = 0;
    while (ctx->size[n_vec] != 0) {
        total_size += ctx->size[n_vec];
        n_vec++;
    }
    DMA_Vec dma_vec = (DMA_Vec){n_vec, total_size, g_malloc(sizeof(DMA_Vec_Entry) * n_vec)};
    for (int i = 0; i < n_vec; i++) {
        dma_vec.vec[i] = (DMA_Vec_Entry){NULL, ctx->next_addr[i], ctx->size[i], ISC_READ_FLASH};
    }
    int old_nvec = task->dma_vec.nvec;
    if (old_nvec > 0)
        g_free(task->dma_vec.vec);
    task->dma_vec = dma_vec;
    alloc_task_buf(task);
    return;
}

void alloc_task_buf(ISC_Task* task) {
    runtime_log("alloc task buf\n");
    if (task->in_buf)
        free_buf(task->in_buf);
    if (task->out_buf)
        free_buf(task->out_buf);
    task->in_buf = alloc_buf(task->dma_vec.total_size);
    uint32_t out_buf_size = (ret_val_size[task->cmd.func_id] == 0) ? \
                        task->in_buf->size : ret_val_size[task->cmd.func_id];
    task->out_buf = alloc_buf(out_buf_size);
    
    uint32_t offset = 0;
    for (int i = 0; i < task->dma_vec.nvec; i++) {
        task->dma_vec.vec[i].buf = task->in_buf->space + offset;
        offset += task->dma_vec.vec[i].size;
    }
}

uint16_t host_dma(FemuCtrl *n, NvmeRequest *req, void* buf, int data_size, int is_write) {
    runtime_log("host_dma\n");
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
        runtime_log("host_dma write\n");
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
    runtime_log("dma length: %ld\n", data_offset);
    qemu_sglist_destroy(qsg);

    // print buf content
    for (int i = 0; i < 50; i++) {
        runtime_log("%d ", ((char*)buf)[i]);
    }
    return NVME_SUCCESS;
}
