extern "C" {

// Export as C function symbols
// Place all function prototypes here
int smAlloca();
void **smDeref(int idx);
void *smLoad(int idx);
void smStore(int idx, void *value);

}

/* TODO ALL: Change to hash table and random index */

static void* pool[100];
static int cnt = 0;

int smAlloca() {
    return cnt++;
}

void **smDeref(int idx) {
    return &pool[idx];
}

void smStore(int idx, void *value) {
    pool[idx] = value;
}

void *smLoad(int idx) {
    return pool[idx];
}
