#include <stdlib.h>

void **__sm_pool;
int __sm_sp = 0;
static int cap = 1;

int __sm_alloca() {
    __sm_sp++;
    if (__sm_pool == NULL || __sm_sp > cap) {
        cap *= 2;
        __sm_pool = (void **) realloc(__sm_pool, cap * sizeof(void *));
    }
    return __sm_sp++;
}
