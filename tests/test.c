#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void f1(const char *s) {
	printf("F1: %s\n", s);
}

static void f2(const char *s) {
	printf("F2: %s\n", s);
}

int main(int argc, char const *argv[]) {
	void (*fptr)(const char *);

	/* Prevent segfault */
	if (argc < 3)
		return 1;

	if (strcmp(argv[1], "1") == 0)
		fptr = f1;
	else
		fptr = f2;
	fptr(argv[2]);
	return 0;
}
