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

static void T() {
    printf("true\n");
}

static void F() {
    printf("false\n");
}

static void test_1(int i);
static void test_2(int i);
static void test_3(struct bar *b, int i);

static void test_1(int i) {
    void (*fptr)();
    fptr = i ? T : F;
    fptr();
    printf("* test_2\n");
    test_2(i);
}

static void test_2(int i) {
    void (*fptr)();
    struct foo f;
    struct bar b;
    if (i) {
        fptr = T;
        f.func = T;
        // b.f1 = T;
        // b.f2 = F;
    } else {
        fptr = F;
        f.func = F;
        // b.f1 = F;
        // b.f2 = T;
    }
    test_3(&b, i);
    fptr();
    f.func();
    b.f1();
    b.f2();
}

static void test_3(struct bar *b, int i) {
    if (i) {
        b->f1 = T;
        b->f2 = F;
    } else {
        b->f1 = F;
        b->f2 = T;
    }
}

int main(int argc, char const *argv[]) {
    /* Prevent segfault */
    if (argc < 2)
        return 1;

    int val = atoi(argv[1]);

    printf("* test_1\n");
    test_1(val);
    printf("* test_1\n");
    test_1(val);

    return 0;
}
