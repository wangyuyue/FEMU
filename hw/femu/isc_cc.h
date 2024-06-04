#ifdef __cplusplus
extern "C" {
#endif

#include "test_function.h"

void hello_world(void);

int print_bucket(void*, int, void*, int, TaskContext*);

int bighash_lookup(void*, int, void*, int, TaskContext*);

int bighash_remove(void*, int, void*, int, TaskContext*);

#ifdef __cplusplus
};
#endif