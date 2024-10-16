#include "lru_cache.h"

LruNode* createNode(int key, int buffer_index) {
    LruNode* node = (LruNode*)malloc(sizeof(LruNode));
    node->key = key;
    node->buffer_index = buffer_index;
    node->prev = node->next = NULL;
    node->hash_next = NULL;
    return node;
}

LRUCache* createCache(Array2D buffer) {
    LRUCache* cache = (LRUCache*)malloc(sizeof(LRUCache));
    cache->head = cache->tail = NULL;
    cache->buffer = buffer;
    cache->size = 0;
    cache->capacity = buffer.dim_o;
    for (int i = 0; i < HASHMAP_SIZE; i++) {
        cache->hashMap[i] = NULL;
    }
    return cache;
}

int hash(int key) {
    return key % HASHMAP_SIZE;
}

// Insert into hashMap with separate chaining
void insertHashMap(LRUCache* cache, LruNode* node) {
    int index = hash(node->key);
    node->hash_next = cache->hashMap[index];
    cache->hashMap[index] = node;
}

// Find node in hashMap
LruNode* findNode(LRUCache* cache, int key) {
    int index = hash(key);
    LruNode* node = cache->hashMap[index];
    while (node) {
        if (node->key == key) {
            return node;
        }
        node = node->hash_next;
    }
    return NULL;
}

void removeFromHashMap(LRUCache* cache, LruNode* node) {
    int index = hash(node->key);
    LruNode* curr = cache->hashMap[index];
    LruNode* prev = NULL;
    while (curr) {
        if (curr == node) {
            if (prev) {
                prev->hash_next = curr->hash_next;
            } else {
                cache->hashMap[index] = curr->hash_next;
            }
            return;
        }
        prev = curr;
        curr = curr->hash_next;
    }
}

void moveToHead(LRUCache* cache, LruNode* node) {
    assert (cache->head && cache->tail);
    if (node == cache->head) return;

    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (node == cache->tail) cache->tail = node->prev;

    node->next = cache->head;
    node->prev = NULL;
    cache->head->prev = node;
    cache->head = node;
}

void removeTail(LRUCache* cache) {
    if (!cache->tail) return;

    LruNode* tail = cache->tail;
    removeFromHashMap(cache, tail);

    if (tail->prev) {
        tail->prev->next = NULL;
    } else {
        cache->head = NULL;
    }
    cache->tail = tail->prev;
    free(tail);
    cache->size--;
}

void put(LRUCache* cache, int key, const void* value) {
    LruNode* node = findNode(cache, key);

    if (node) {
        return;
    } else {
        int new_buffer_index;
        if (cache->size < cache->capacity) {
            new_buffer_index = cache->size;
        } else {
            new_buffer_index = cache->tail->buffer_index;
            removeTail(cache);
        }

        // Create a new node with the assigned buffer index
        LruNode* newNode = createNode(key, new_buffer_index);
        Array1D entry = at_ith_2d(cache->buffer, newNode->buffer_index);
        memcpy(entry.data, value, entry.dim * entry.elem_size);

        // Insert new node into the hashmap and move it to the head of the list
        insertHashMap(cache, newNode);
        newNode->next = cache->head;
        if (cache->head) cache->head->prev = newNode;
        cache->head = newNode;
        if (!cache->tail) cache->tail = newNode;
        cache->size++;
    }
}

void* get(LRUCache* cache, int key) {
    LruNode* node = findNode(cache, key);

    if (node) {
        moveToHead(cache, node);
        Array1D entry = at_ith_2d(cache->buffer, node->buffer_index);
        return entry.data;
    }
    return NULL;
}

void printCache(LRUCache* cache) {
    LruNode* current = cache->head;
    printf("Cache: ");
    while (current) {
        Array1D entry = at_ith_2d(cache->buffer, current->buffer_index);
        printf("(%d:%s) ", current->key, (char*)entry.data);
        current = current->next;
    }
    printf("\n");
}

int test_lru(void) {
    printf("LRU Cache\n");
    Array2D buffer = {10, 128, sizeof(float), malloc(10 * 128 * sizeof(float))};

    LRUCache* cache = createCache(buffer);
    put(cache, 1, "one");
    put(cache, 2, "two");
    put(cache, 3, "three");
    put(cache, 4, "four");
    put(cache, 5, "five");
    put(cache, 6, "six");
    put(cache, 7, "seven");
    put(cache, 8, "eight");
    put(cache, 9, "nine");
    put(cache, 10, "ten");

    printCache(cache);

    get(cache, 3);
    printCache(cache);

    put(cache, 11, "eleven");
    printCache(cache);

    get(cache, 5);
    printCache(cache);

    put(cache, 12, "twelve");
    printCache(cache);

    return 0;
}

// int main() {
//     test_lru();
//     return 0;
// }