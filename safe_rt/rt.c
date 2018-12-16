#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#define BLOCK_SZ 16
#define PTR_MEM_SZ(i) ((i) * sizeof(void *))

int __sm_sp = 0;
static void ***block_table;
static int table_sz = 0;

static inline void **sm_alloca() {
    int block_num = __sm_sp >> 4;
    if (block_num == table_sz) {
        int new_sz = table_sz ? table_sz * 2 : 1;
        block_table = realloc(block_table, PTR_MEM_SZ(table_sz));
        // Zero out the new space
        memset(&block_table[table_sz], 0, PTR_MEM_SZ(new_sz - table_sz));
        table_sz = new_sz;
    }
    if (!block_table[block_num])
        block_table[block_num] = malloc(PTR_MEM_SZ(BLOCK_SZ));
    fprintf(stderr, "__sm_alloca %d, %d\n", __sm_sp, table_sz << 4);
    return &block_table[block_num][__sm_sp++ & 0xF];
}

void **__sm_alloca() {
    return sm_alloca();
}

void **__sm_malloc(void **ua) {
    void **sa = sm_alloca();
    *sa = *ua;
    return sa;
}

void *__sm_load(void **sa, void **ua) {
    assert(*sa == *ua);
    return *sa;
}
