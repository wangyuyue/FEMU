#include <stdio.h>
#include <string.h>
#include "db_types.h"
#include "test_function.h"

#if 0
#define dbg_print(...) printf(__VA_ARGS__)
#else
#define dbg_print(...) do {} while(0)
#endif

static __inline void dump_page(void* ptr) {
    char* page = (char*) ptr;
    for (int i = 0; i < 8; i++) {
        char tmp[65];
        for (int j = 0; j < 64; j++)
            tmp[j] = page[64 * i + j];
        printf("%s\n", tmp);
    }
}

static __inline int key_exists(unsigned long const key, Node *node) {
    if (node == NULL)
        return -1;
    for (int i = 0; i < NODE_CAPACITY; ++i) {
        if (node->key[i] == key) {
            return 1;
        }
    }
    return 0;
}

static __inline ptr__t nxt_node(unsigned long key, Node *node) {
    if (node == NULL)
        return -1;
    for (int i = 1; i < NODE_CAPACITY; ++i) {
        if (key < node->key[i]) {
            return node->ptr[i - 1];
        }
    }
    /* Key wasn't smaller than any of node->key[x], so take the last ptr */
    return node->ptr[NODE_CAPACITY - 1];
}

/* State flags */
#define AT_VALUE 1

/* Mask to prevent out of bounds memory access */
#define EBPF_CONTEXT_MASK SG_KEYS - 1

int kv_get(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    KVGetContext *query = (KVGetContext*)(ctx->data);

    Node *node = (Node *) buf_in;
    KVGetRetVal* ret_val = (KVGetRetVal*)buf_out;
    /* Three cases:
     *
     * 1. We've found the log offset in the previous iteration and are
     *    now reading the value into the query result. If there are more
     *    keys to process, start again at the node.
     *
     * 2. We've found a leaf node and need to a) verify the key exists and 2)
     *    get the log offset and make one more resubmission to read the value.
     *    If the keys is missing, but there are more keys to process, start again
     *    at the root.
     *
     * 3. We're in an internal node and need to keep traversing the B+ tree
     */

    /* Case 1: read value into query result */
    dbg_print("kv-get: entered\n");
    if (query->state_flags & AT_VALUE) {
        dbg_print("simplekv-bpf: case 1 - value found\n");

        ptr__t offset = query->value_ptr & (BLK_SIZE - 1);
        memcpy(ret_val->val, (char*)node + offset, sizeof(val__t));
            
        ctx->done = 1;
        return 0;
    }

    /* Case 2: verify key & submit read for block containing value */
    if (node->type == LEAF) {
        dbg_print("simplekv-bpf: case 2 - verify key & get last block\n");

        query->state_flags = REACHED_LEAF;
        if (!key_exists(query->params.key, node)) {
            dbg_print("simplekv-bpf: key doesn't exist\n");

            ret_val->found = '!';

            ctx->done = 1;
            return 0;
        }
        query->state_flags = AT_VALUE;
        query->value_ptr = decode(nxt_node(query->params.key, node));
        /* Need to submit a request for base of the block containing our offset */
        ptr__t base = query->value_ptr & ~(BLK_SIZE - 1);
        ctx->next_addr[0] = base;
        ctx->size[0] = BLK_SIZE;
        dbg_print("key is %ld, next address: %p\n", query->params.key, (void*)(ctx->next_addr[0] & ~(BLK_SIZE - 1)));
        return 0;
    }

    /* Case 3: at an internal node, keep going */
    dbg_print("simplekv-bpf: case 3 - internal node\n");
    ctx->next_addr[0] = decode(nxt_node(query->params.key, node));
    dbg_print("key is %ld, next address: %p\n", query->params.key, (void*)(ctx->next_addr[0] & ~(BLK_SIZE - 1)));
    ctx->size[0] = BLK_SIZE;
    return 0;
}
