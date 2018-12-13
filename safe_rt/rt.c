#include <stdlib.h>

/* TODO ALL: Change to hash table and random index */

void **__sm_pool;
int __sm_sp = 0;
static int cap = 1;

int smAlloca() {
    __sm_sp++;
    if (__sm_pool == NULL || __sm_sp > cap) {
        cap *= 2;
        __sm_pool = (void **) realloc(__sm_pool, cap * sizeof(void *));
    }
    return __sm_sp++;
}

void **smDeref(int idx) {
    return &__sm_pool[idx];
}

void smStore(int idx, void *value) {
    __sm_pool[idx] = value;
}

void *smLoad(int idx) {
    return __sm_pool[idx];
}
