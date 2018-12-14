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

static void f1() {
	printf("F1\n");
}

static void f2() {
	printf("F2\n");
}

int main(int argc, char const *argv[]) {
	void (*fptr)();
	struct foo f;
	struct bar b;

	/* Prevent segfault */
	if (argc < 2)
		return 1;

	if (strcmp(argv[1], "1") == 0) {
		fptr = f1;
		f.func = f2;
	} else {
		fptr = f2;
		f.func = f1;
	}

	fptr();
	f.func();
	return 0;
}
