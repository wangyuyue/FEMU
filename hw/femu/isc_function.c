#include "isc_function.h"
#include "test_function.h"

// typedef struct isc_task {
//     NvmeCmd cmd,
//     char* task_name
// }

void free_buf(Buffer* buf) {
    buf->ref -= 1;
    if (buf->ref > 1)
        return;
    g_free(buf->space);
    g_free(buf);
}

Buffer* alloc_buf(int size) {
    Buffer* buf = g_malloc(sizeof(Buffer));
    buf->space = g_malloc(size);
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


ISC_Task task;

void* worker(void* arg) {
    printf("hello world\n");
    range_filter(task.in_buf->space, task.in_buf->size, task.out_buf->space, task.out_buf->size, NULL);
    return 0;
}

void* runtime(void* arg) {
    FemuCtrl* n = (FemuCtrl*)arg;
    n->workers = malloc(sizeof(QemuThread) * n->n_worker);
    task.task_id = 512;
    task.valid = 0;
    task.busy = 0;
    task.completed = 0;
    // for (int i = 0; i < n->n_worker; i++) {
    //     qemu_thread_create(&n->workers[i], "isc-worker", worker_thread, NULL);
    // }
    runtime_log("runtime starts running\n");
    while(1) {
        NvmeRequest* req;
        int rc;
        if (femu_ring_count(n->isc_task_queue)) {
            req = NULL;
            rc = femu_ring_dequeue(n->isc_task_queue, (void *)&req, 1);
            if (rc != 1) {
                femu_err("dequeue from isc_task_queue request failed\n");
            }
            assert(req);
            NvmeComputeCmd* comp = (NvmeComputeCmd*)&(req->cmd);
            int dataflow_type = comp->dataflow_type;
            assert((dataflow_type & BUF_TO_SSD) == 0);
            assert((dataflow_type & HOST_TO_BUF) == 0);

            if (comp->dataflow_type & SSD_TO_BUF) {
                buf_rw(n, req->ns, &(req->cmd), req);
                int poller_id = n->multipoller_enabled ? req->sq->sqid : 1;
                runtime_log("(pid %ld) get a compute req from poller %d, query ftl\n", pthread_self(), poller_id);
                rc = femu_ring_enqueue(n->to_ftl[poller_id], (void *)&req, 1);
                if (rc != 1) {
                    femu_err("enqueue to ftl request failed\n");
                }
            }
        }

        if (femu_ring_count(n->to_runtime)) {
            req = NULL;
            rc = femu_ring_dequeue(n->to_runtime, (void *)&req, 1);
            if (rc != 1) {
                femu_err("dequeue from to_runtime request failed\n");
            }
            assert(req);
            pqueue_insert(n->runtime_pq, req);
        }

        while ((req = pqueue_peek(n->runtime_pq))) {
            uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            if (now < req->expire_time) {
                break;
            }
            runtime_log("compute req complete read flash\n");
            pqueue_pop(n->runtime_pq);

            qemu_thread_create(n->workers, "isc-worker", worker, arg, QEMU_THREAD_JOINABLE);
            qemu_thread_join(n->workers);

            buf_dma(n, req->ns, &(req->cmd), req, task.out_buf->space, task.out_buf->size);

            req->cqe.res64 = task.task_id << 32 | task.out_buf->size; // isc task id and buf size
            int poller_id = n->multipoller_enabled ? req->sq->sqid : 1;
            runtime_log("finish a compute req, send to poller %d\n", poller_id);
            rc = femu_ring_enqueue(n->to_poller[poller_id], (void *)&req, 1);
        }
    }
}


void init_runtime(FemuCtrl* n) {
    runtime_log("start init runtime\n");
    n->n_worker = 1;
    n->to_runtime = femu_ring_create(FEMU_RING_TYPE_MP_SC, FEMU_MAX_INF_REQS);
    n->isc_task_queue = femu_ring_create(FEMU_RING_TYPE_MP_SC, FEMU_MAX_INF_REQS);

    n->runtime_pq = pqueue_init(FEMU_MAX_INF_REQS, cmp_pri, get_pri, set_pri,
                                get_pos, set_pos);
    if (!n->runtime_pq) {
        femu_err("fail to create pqueue for isc runtime\n");
        abort();
    }

    qemu_thread_create(&n->runtime, "isc-runtime", runtime, (void*)n, QEMU_THREAD_JOINABLE);
    runtime_log("finish init runtime\n");
}

uint16_t buf_rw(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req)
{
    NvmeComputeCmd *comp = (NvmeComputeCmd *)cmd;
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
    runtime_log("slba: %ld, data size %ld\n", slba, data_size);

    req->is_write = 0;

    err = femu_nvme_rw_check_req(n, ns, cmd, req, slba, elba, nlb, ctrl,
                                 data_size, meta_size);
    if (err)
        return err;

    req->slba = slba;
    req->status = NVME_SUCCESS;
    req->nlb = nlb;

    task.in_buf = alloc_buf(data_size);
    void* buf = task.in_buf->space;
    task.out_buf = alloc_buf(data_size);

    SsdDramBackend *b = n->mbe;
    void* mb = b->logical_space;
    if (b->femu_mode != FEMU_BBSSD_MODE) {
        error_report("FEMU: buf_rw only support black-box SSD");
    }
    if (req->is_write) {
        memcpy(mb + data_offset, buf, data_size);
    } else {
        memcpy(buf, mb + data_offset, data_size);
    }

    return 0;
}

uint16_t buf_dma(FemuCtrl *n, NvmeNamespace *ns, NvmeCmd *cmd, NvmeRequest *req, void* buf, int data_size) {
    NvmeComputeCmd *comp = (NvmeComputeCmd *)cmd;

    uint64_t prp1 = le64_to_cpu(comp->prp1);
    uint64_t prp2 = le64_to_cpu(comp->prp2);
    printf("prp1: %ld, prp2: %ld, data size %d\n", prp1, prp2, data_size);

    if (nvme_map_prp(&req->qsg, &req->iov, prp1, prp2, data_size, n)) {
        nvme_set_error_page(n, req->sq->sqid, cmd->cid, NVME_INVALID_FIELD,
                            offsetof(NvmeRwCmd, prp1), 0, ns->id);
        return NVME_INVALID_FIELD | NVME_DNR;
    }

    req->status = NVME_SUCCESS;

    int is_write = 0;
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
            error_report("FEMU: dma_memory_rw error");
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