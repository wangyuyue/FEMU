#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "test_function.h"

int range_filter(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    assert(buf_in != NULL);
    assert(buf_out != NULL);
    struct {
        int n_integer;
        int min_val;
        int max_val;
        int data[];
    } *params;
    int *data_buf;
    if (ctx->len == 0) {
        params = buf_in;
        data_buf = params->data;
    } else {
        params = (void*)ctx->data;
        data_buf = buf_in;
    }
    struct {
        int n_out;
        int data[];
    } *filtered;
    filtered = buf_out;
    filtered->n_out = 0;
    for (int i = 0; i < params->n_integer; i++) {
        if (params->min_val <= data_buf[i] && data_buf[i] <= params->max_val) {
            filtered->data[filtered->n_out++] = data_buf[i];
        }
    }
    return 0;
}

int sum(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    assert(buf_in != NULL);
    assert(buf_out != NULL);
    struct {
        int n_integer;
        int data[];
    } *params;
    int* data_buf;
    if (ctx->len == 0){
        params = buf_in;
        data_buf = params->data;
    } else {
        params = (void*)ctx->data;
        data_buf = buf_in;
    } 
    int* result = buf_out;
    for (int i = 0; i < params->n_integer; i++) {
        *result += data_buf[i];
    }
    return 0;
}

int traverse(void* buf_in, int size_in, void* buf_out, int size_out, TaskContext* ctx) {
    assert(buf_in != NULL);
    assert(buf_out != NULL);
    struct {
        int next_blk;
        char str[];
    } *params;
    params = buf_in;
    assert(ctx != NULL);

    if (params->next_blk == -1) {
        ctx->done = 1;
    } else {
        ctx->done = 0;
        ctx->next_addr[0] = params->next_blk * 512;
        ctx->size[0] = 512;
    }
    char* from_str = params->str;
    char* to_str = ctx->data + strlen(ctx->data);

    while(*from_str)
        *(to_str++) = *(from_str++);
    *to_str = ' ';
    printf("%d, %s \n", params->next_blk, params->str);
    return 0;
}