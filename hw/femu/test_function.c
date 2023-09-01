#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "test_function.h"

int range_filter(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
    assert(buf_in != NULL);
    assert(buf_out != NULL);
    int* params, *data_buf;
    int n_integer, min_val, max_val;
    if (arg == NULL){
        params = buf_in;
        data_buf = (int*)buf_in + 3;
    } else {
        params = arg;
        data_buf = (int*)buf_in;
    }
    n_integer = params[0];
    min_val = params[1];
    max_val = params[2];
    int* filter = (int*)buf_out + 1;
    int n_out = 0;
    for (int i = 0; i < n_integer; i++) {
        if (min_val <= data_buf[i] && data_buf[i] <= max_val) {
            filter[n_out++] = data_buf[i];
        }
    }
    *(int*)buf_out = n_out;
    return 0;
}

int sum(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
    assert(buf_in != NULL);
    assert(buf_out != NULL);
    int* params, *data_buf;
    int n_integer;
    if (arg == NULL){
        params = buf_in;
        data_buf = (int*)buf_in + 1;
    } else {
        params = arg;
        data_buf = (int*)buf_in;
    }
    n_integer = params[0];
    int result = 0;
    for (int i = 0; i < n_integer; i++) {
        result += data_buf[i];
    }
    *(int*)buf_out = result;
    return 0;
}

int traverse(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
    assert(buf_in != NULL);
    assert(buf_out != NULL);

    int next_blk = *(int*)buf_in;
    char* str = (char*)buf_in + sizeof(int);

    tmp_ctx* ctx = (tmp_ctx*)arg;
    
    if (ctx) {
        if (next_blk == -1) {
            ctx->done = 1;
        } else {
            ctx->done = 0;
            ctx->next_addr[0] = next_blk * 512;
            ctx->size[0] = 512;
        }
    }
    char* from_str = str;
    char* to_str = (char*)ctx->content + strlen(ctx->content);

    while(*from_str)
        *(to_str++) = *(from_str++);
    *to_str = ' ';
    printf("%d, %s \n", next_blk, str);
    return 0;
}

void* content_addr(void* ctx) {
    return &((tmp_ctx*)ctx)->content;
}