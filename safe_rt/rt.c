#include <stdlib.h>
#include <stdio.h>

#define INIT_SZ 4

void **__sm_pool;
int __sm_sp = 0;
static int pool_sz = 0;

int __sm_alloca() {
    if (__sm_sp == pool_sz) {
        pool_sz = pool_sz ? pool_sz * 2 : INIT_SZ;
        __sm_pool = (void **) realloc(__sm_pool, pool_sz * sizeof(void *));
    }
    // Debug message
    fprintf(stderr, "__sm_alloca: %d, %d\n", __sm_sp, pool_sz);
    return __sm_sp++;
}
