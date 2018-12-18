#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct foo {
    int i;
    void (*func)();
};

struct bar {
    void (*f1)();
    int i;
    void (*f2)();
};

struct baz {
    size_t i;
    void (*f)(size_t s);
};

static void T() {
    printf("T\n");
}

static void F() {
    printf("F\n");
}

static void good(size_t s) {
    printf("Proper control flow!\n");
}

static void bad(size_t s) {
    printf("Hijacked control flow!\n");
}

static void test_1(int i);
static void test_2(struct bar *b);
static void test_3(struct foo *f);

static void vuln(int off, size_t val) {
    struct baz b;
    size_t buf[4];
    printf("Vuln offset: %ld\n", ((void * )&b.f - (void *) buf) / (ssize_t) sizeof(size_t));
    b.f = good;
    buf[off] = val;
    b.f(buf[off]);
}

static void test_1(int i) {
    void (*fptr)();
    struct foo f;
    struct bar b;
    b.i = i;
    if (i) {
        fptr = T;
        f.func = T;
        b.f1 = T;
        b.f2 = T;
    } else {
        fptr = F;
        f.func = F;
        b.f1 = F;
        b.f2 = F;
    }
    printf("* test_2\n");
    test_2(&b);
    printf("* test_1\n");
    fptr();
    f.func();
    b.f2();
}

static void test_2(struct bar *b) {
    b->f1();
    b->f2 = b->i ? F : T;
    struct foo *f = malloc(sizeof(*f));
    f->i = b->i;
    f->func = b->f1;
    printf("* test_3\n");
    test_3(f);
    printf("* test_2\n");
    f->func();
    free(f);
}

static void test_3(struct foo *f) {
    f->func();
    f->func = f->i ? F : T;
}

int main(int argc, char const *argv[]) {
    /* Prevent segfault */
    if (argc < 2)
        return 1;

    int val = atoi(argv[1]);

    /* Test control flow hijack */
    printf("------- Control Flow Test -------\n");
    vuln(val, (size_t) bad);

    printf("------- Correctness Test -------\n");
    printf("* test_1\n");
    test_1(val);

    return 0;
}
