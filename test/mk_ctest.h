#ifndef _MK_CTEST_H_
#define _MK_CTEST_H_

#include <stdio.h>

#define FAILED		-1
#define PASSED		0
#define ERR			1

typedef int (*test_func_t)(int);
typedef const char *(*print_func_t)(int);

#define COLUMN	48

struct mock {
	char foo[16];
	int bar;
};

static inline void
UNIT_TEST(const char *name, test_func_t f, size_t arg)
{
	printf("  %-*s", COLUMN - 10, name);
	int result = f(arg);
	if (PASSED == result)
		printf("\033[32m[%6s]\033[0m\n", "PASSED");
	else if (FAILED == result)
		printf("\033[31m[%6s]\033[0m\n", "FAILED");
	else if (ERR == result)
		printf("\033[31m[%6s]\033[0m\n", "ERROR");
	else
		printf("\033[31m[%6s]\033[0m\n", "UNDEFINED");
}

static inline void
PRINT_RESULT(const char *name, print_func_t f, size_t arg)
{

	printf("  %-*s", COLUMN - 10, name);
	const char *result = f(arg);
	printf("[%6s]\n", result);
}

#endif
