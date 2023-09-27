#include "isc_cc.h"
#include "test_function.h"
#include <stdio.h>

class Hello {
public:
    int x;
};

extern "C" {
    void hello_world() {
        Hello hello;
        hello.x = 5;
        printf("hello, this is %d\n", hello.x);
    }
}