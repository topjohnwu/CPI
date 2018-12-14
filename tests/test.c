#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct foo {
	int i;
	void (*func)();
};

struct bar {
	void (*func)();
	int i;
};

static void T() {
	printf("true\n");
}

static void F() {
	printf("false\n");
}

static void test_1(int i) {
    void (*fptr)();
    fptr = i ? T : F;
    fptr();
}

static void test_2(int i) {
    void (*fptr)();
    struct foo f;
    struct bar b;
    if (i) {
        fptr = T;
        f.func = T;
        b.func = T;
    } else {
        fptr = F;
        f.func = F;
        b.func = F;
    }
    fptr();
    f.func();
    b.func();
}

int main(int argc, char const *argv[]) {
	/* Prevent segfault */
	if (argc < 2)
		return 1;

	int val = atoi(argv[1]);

	for (int i = 0; i < 3; ++i) {
        printf("* test_1\n");
	    test_1(val);
        printf("* test_2\n");
	    test_2(val);
	}

	return 0;
}
