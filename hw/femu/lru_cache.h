#ifndef __LRU_CACHE_H__
#define __LRU_CACHE_H__

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "array.h"

typedef struct LruNode {
    int key;
    int buffer_index;
    struct LruNode* prev;
    struct LruNode* next;
    struct LruNode* hash_next; // Pointer to the next node in the hash bucket
} LruNode;

#define HASHMAP_SIZE 10000
typedef struct {
    LruNode* head;
    LruNode* tail;
    LruNode* hashMap[HASHMAP_SIZE];
    Array2D buffer;
    int size;
    int capacity;
} LRUCache;

LruNode* createNode(int key, int buffer_index);
LRUCache* createCache(Array2D buffer);
int hash(int key);
void insertHashMap(LRUCache* cache, LruNode* node);
LruNode* findNode(LRUCache* cache, int key);
void removeFromHashMap(LRUCache* cache, LruNode* node);
void moveToHead(LRUCache* cache, LruNode* node);
void removeTail(LRUCache* cache);
void put(LRUCache* cache, int key, const void* value);
void* get(LRUCache* cache, int key);
void printCache(LRUCache* cache);
int test_lru(void);

#endif