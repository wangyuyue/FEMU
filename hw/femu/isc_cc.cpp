#include "isc_cc.h"
#include "test_function.h"
#include <iostream>
#include <folly/Random.h>

class Hello {
public:
    int x;
};

extern "C" {
    void hello_world() {
        Hello hello;
        hello.x = 5;
        
        std::cout << "hello, this is " << hello.x + 1 << folly::Random::rand32(0, 1000) << std::endl;
    }
}