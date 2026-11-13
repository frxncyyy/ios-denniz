/* 0482 host test: which compressor mode the STARTUP hook leaves for vm_fault_init.  cc -std=c11 -Wall -Wextra -Werror */
#include <stdbool.h>
#include <stdio.h>
#include "../b/bsd/kern/nord2_vmcomp.h"
int main(void)
{
	int bad = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); bad++; } } while (0)
	EXPECT(nord2_vmcomp_pick(false, false, 0, 4) == 2);             /* no boot-arg: no swap */
	EXPECT(nord2_vmcomp_pick(false, true, 4, 4) == 4);              /* n2vc=4: as before */
	EXPECT(nord2_vmcomp_pick(false, true, 2, 4) == 2);
	for (int v = -1; v <= 64; v++) {                                /* never an invalid mode, whatever n2vc says */
		int m = nord2_vmcomp_pick(false, true, v, 4);
		EXPECT(m == 2 || m == 4);
		EXPECT((m == 4) == (v == 4));
		EXPECT(nord2_vmcomp_pick(true, true, v, 4) == 4 && nord2_vmcomp_pick(true, false, v, 4) == 4);   /* vm_compressor= present: untouched */
	}
	printf("%s\n", bad ? "VMCOMP_HOST_TEST FAILED" : "VMCOMP_HOST_TEST OK");
	return bad != 0;
}
