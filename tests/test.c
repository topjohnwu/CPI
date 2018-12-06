#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void f1() {
	printf("F1\n");
}

static void f2() {
	printf("F2\n");
}

int main(int argc, char const *argv[]) {
	void (*fptr)(void);

	if (strcmp(argv[1], "1") == 0)
		fptr = f1;
	else
		fptr = f2;
	fptr();
	return 0;
}
