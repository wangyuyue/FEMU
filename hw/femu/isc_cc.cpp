#include "isc_cc.h"
#include "test_function.h"
#include <iostream>
#include <folly/Random.h>

#include "flash-cache/bighash/Bucket.h"

using namespace cache;
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

    int print_bucket(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
        std::cout << "print_bucket\n";
        MutableBufferView buf_view(size_in, (uint8_t*)buf_in);
        Bucket* bucket{nullptr};
        bucket = reinterpret_cast<Bucket*>(buf_view.data());
        bucket->traverse_print();
        return 0;
    }

    int bighash_lookup(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
        std::cout << "lookup\n" << size_in << " " << size_out << "\n";
        MutableBufferView buf_view(size_in, (uint8_t*)buf_in);
        Bucket* bucket{nullptr};
        bucket = reinterpret_cast<Bucket*>(buf_view.data());
        std::cout << "checkpoint 1\n";
        auto entry_ = reinterpret_cast<details::BucketEntry*>(APP_CONTEXT(arg));
        std::cout << entry_->key().size() << entry_->keyString() << "\n";
        auto hashed_key = entry_->hashedKey();
        
        std::cout << "checkpoint 2\n";
        bucket->find(hashed_key);
        auto value = bucket->find(hashed_key);
        // auto value = BufferView{5, (uint8_t*)("12345")};
        details::BucketEntry::create(MutableBufferView{(size_t)size_out, (uint8_t*) buf_out}, hashed_key, value);
        std::cout << "lookup done\n";
        return 0;
    }

    int bighash_remove(void* buf_in, int size_in, void* buf_out, int size_out, void* arg) {
        std::cout << "remove\n";
        MutableBufferView buf_view(size_in, (uint8_t*)buf_in);
        Bucket* bucket{nullptr};
        bucket = reinterpret_cast<Bucket*>(buf_view.data());
        auto entry_ = reinterpret_cast<details::BucketEntry*>(APP_CONTEXT(arg));
        auto hashed_key = entry_->hashedKey();
        
        Buffer valueCopy;
        DestructorCallback cb = [&valueCopy](
                            HashedKey, BufferView value, DestructorEvent) {
            valueCopy = Buffer{value};
        };
        int ret = bucket->remove(hashed_key, cb);

        details::BucketEntry::create(MutableBufferView{(size_t)size_out, (uint8_t*) buf_out}, hashed_key, valueCopy.view());
        if (ret == 1) {
            // rebuild the bloom filter
        }
        return 0;
    }
}