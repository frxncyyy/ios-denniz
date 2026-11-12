/* SPDX-License-Identifier: MIT */
/* t_main.c -- V30 host tests: string/memory, printf, scanf, the guest heap, qsort, mmap. */
#include <stdio.h>

#include "n2a32.h"
#include "n2elf.h"
#include "v30test.h"

void test_heap_threads(void);

int main(void)
{
	t_init();
	test_string();
	test_format();
	test_fuzz();
	test_scanf();
	test_heap();
	test_heap_threads();
	test_qsort();
	test_mmap();
	printf("V30RESULT %d/%d\n", t_pass, t_total);
	return t_pass == t_total ? 0 : 1;
}
