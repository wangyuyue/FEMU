#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include "test_function.h"

int range_filter(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
    printf("input number %d\n", *(int*)buf_in);
    assert(buf_in != NULL);
    assert(buf_out != NULL);
    int* params, *data_buf;
    int n_integer, min_val, max_val;
    if (arg == NULL){
        params = buf_in;
        data_buf = (int*)((char*)buf_in + sizeof(int)*3);
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