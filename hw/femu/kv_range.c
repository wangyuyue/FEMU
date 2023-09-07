#include <stdio.h>
#include <string.h>
#include "db_types.h"
#include "test_function.h"

/* Mask to prevent out of bounds memory access */
#define KEY_MASK (RNG_KEYS - 1)

#if 0
#define dbg_print(...) printf(__VA_ARGS__)
#else
#define dbg_print(...) do {} while(0)
#endif

#if 0
static __inline void dump_node(void* ptr) {
    Node* node = (Node*)ptr;
    for (int i = 0; i < NODE_CAPACITY; i++) {
        printf("%lu, ", node->key[i]);
        if (i % 6 == 0)
            printf("\n");
    }
    printf("\n");
}
#else
static __inline void dump_node(void* ptr) { }
#endif

static __inline ptr__t nxt_node(unsigned long key, Node *node) {
    /* Safety: NULL is never passed for node, but mr. verifier doesn't know that */
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

static __inline unsigned int process_leaf(TaskContext *context, Node *node, KVRangeRetVal* ret_val) {
    dbg_print("simplekv-bpf-range: - process_leaf\n");
    dump_node(node);
    KVRangeContext *query = (KVRangeContext*)APP_CONTEXT(context);
    key__t first_key = query->params.flags & RNG_BEGIN_EXCLUSIVE ? query->params.range_begin + 1 : query->params.range_begin;
    unsigned int end_inclusive = query->params.flags & RNG_END_INCLUSIVE;
    
    /* Iterate over keys in leaf node */
    unsigned int *i = &query->params._node_key_ix;
    for(;;) {
        /* Iterate over keys in leaf node */
        for (; *i < NODE_CAPACITY && query->len < RNG_KEYS; ++(*i)) {
            key__t curr_key = node->key[*i & KEY_MASK];
            dbg_print("                      current key %lu\n", curr_key);
            if (curr_key > query->params.range_end || (curr_key == query->params.range_end && !end_inclusive)) {
                /* All done; set state and return 0 */
                dbg_print("                      all done\n");
                mark_range_query_complete(query);
                context->done = 1;
                return 0;
            }
            /* Retrieve value for this key */
            if (curr_key >= first_key) {
                /* Set up the next resubmit to read the value */
                context->next_addr[0] = value_base(decode(node->ptr[*i & KEY_MASK]));
                context->size[0] = BLK_SIZE;

                key__t key = node->key[*i & KEY_MASK];
                ret_val->kv[query->len & KEY_MASK].key = key;

                /* Fixup the begin range so that we don't try to grab the same key again */
                query->params.range_begin = key;
                query->params.flags |= RNG_BEGIN_EXCLUSIVE;

                query->params._state = RNG_READ_VALUE;
                return 0;
            }
        }

        /* Three conditions: Either the query buff is full, or we inspected all keys, or both */

        /* Check end condition of outer loop */
        if (query->len == RNG_KEYS) {
            /* Query buffer is full; need to suspend and return */
            context->done = 1;
            dbg_print("                      query buffer is full\n");
            query->params.range_begin = ret_val->kv[(query->len - 1) & KEY_MASK].key;
            query->params.flags |= RNG_BEGIN_EXCLUSIVE;
            if (*i < NODE_CAPACITY) {
                /* This node still has values we should inspect */
                return 0;
            }

            /* Need to look at next node */
            if (node->next == 0) {
                /* No next node, so we're done */
                mark_range_query_complete(query);
            } else {
                query->params._resume_from_leaf = node->next;
                query->params._node_key_ix = 0;
                query->params._state = RNG_READ_NODE;
            }
            /* Return to user since we marked context->done = 1 at the top of this if block */
            return 0;
        } else if (node->next == 0) {
            dbg_print("                      no more items on the right side\n");
            /* Still have room in query buf, but we've read the entire index */
            mark_range_query_complete(query);
            context->done = 1;
            return 0;
        }

        /*
         * Query buff isn't full, so we inspected all keys in this node
         * and need to get the next node.
         */
        query->params._resume_from_leaf = node->next;
        dbg_print("next resume point is %p\n", (void*)(query->params._resume_from_leaf));
        query->params._state = RNG_READ_NODE;
        query->params._node_key_ix = 0;
        context->next_addr[0] = node->next;
        context->size[0] = BLK_SIZE;
        return 0;
    }
}

static __inline unsigned int process_value(TaskContext *context, Node* node, KVRangeRetVal *ret_val) {
    dbg_print("simplekv-bpf-range: - process_value\n");
    KVRangeContext *query = (KVRangeContext*)APP_CONTEXT(context);
    unsigned int *i = &query->params._node_key_ix;
    unsigned long offset = value_offset(decode(query->_current_node.ptr[*i & KEY_MASK]));

    if (query->params.agg_op == AGG_NONE) {
        memcpy(ret_val->kv[query->len & KEY_MASK].value, (char*)node + offset, sizeof(val__t));
        query->len += 1;
    }
    else if (query->params.agg_op == AGG_SUM) {
        ret_val->agg_value += *(long *) ((char*)node + offset);
    }

    /* TODO: This should be incremented, but not doing so does not affect correctness.
     *   For some reason, if we do increment, the verifier complains in `process_leaf` about
     *   access using the query->_node_key_ix variable to index into the node's key / ptr array.
     *   It seems like this shouldn't be an issue due to the loop invariant, but the verifier
     *   disagrees...
     */
    *i += 1;
    query->params._state = RNG_RESUME;
    return process_leaf(context, &query->_current_node, ret_val);
}

static __inline unsigned int traverse_index(TaskContext *context, Node *node, KVRangeRetVal* ret_val) {
    dbg_print("simplekv-bpf-range: - traverse_index\n");
    dump_node(node);
    KVRangeContext *query = (KVRangeContext*)APP_CONTEXT(context);
    if (node->type == LEAF) {
        query->_current_node = *node;
        return process_leaf(context, node, ret_val);
    }

    /* Grab the next node in the traversal */
    context->next_addr[0] = decode(nxt_node(query->params.range_begin, node));
    context->size[0] = BLK_SIZE;
    return 0;
}

static __inline void update_retval(TaskContext* context, KVRangeRetVal* ret_val) {
    KVRangeContext* query = (KVRangeContext*)APP_CONTEXT(context);
    if (!context->done)
        return;
    ret_val->range_begin = query->params.range_begin;
    ret_val->flags = query->params.flags;
    ret_val->_node_key_ix = query->params._node_key_ix;
    ret_val->_resume_from_leaf = query->params._resume_from_leaf;
    ret_val->len = query->len;
}

static __inline void range(KVRangeContext* query, char* range_string) {
    range_string[0] = query->params.flags & RNG_BEGIN_EXCLUSIVE ? '(' : '[';
    
    sprintf(range_string+1, "%ld, %ld", query->params.range_begin, query->params.range_end);
    
    int len = strlen(range_string);
    range_string[len] = query->params.flags & RNG_END_INCLUSIVE ? ']' : ')';
}

int kv_range(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
    TaskContext* context = (TaskContext*)arg;
    KVRangeContext *query = (KVRangeContext*)APP_CONTEXT(context);
    Node *node = (Node *) buf_in;
    KVRangeRetVal* ret_val = (KVRangeRetVal*)buf_out;
    
#if 0
    char range_string[20];
    memset(range_string, 0, sizeof(range_string));
    range(query, range_string);
    dbg_print("simplekv-bpf-range: - %s\n", range_string);
#endif
    if (query->params._state == RNG_RESUME)
        dbg_print("                     start with resume state\n");

    switch (query->params._state) {
        case RNG_TRAVERSE:
            return traverse_index(context, node, ret_val);
        case RNG_READ_NODE:
            query->params._state = RNG_RESUME;
            /* FALL THROUGH */
        case RNG_RESUME: {
            query->_current_node = *node;
            int ret = process_leaf(context, node, ret_val);
            update_retval(context, ret_val);
            return ret;
        }
        case RNG_READ_VALUE: {
            int ret = process_value(context, node, ret_val);
            update_retval(context, ret_val);
            return ret;
        }
        default:
            context->done = 1;
            return -1;
    }
}