#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *buffer = (char *)malloc(32);
    int total = 0;
    int i;
    /* The allocation must be in the heap, not merely non-null: an address of 0 is
       writable RAM in kernel mode, so a failed malloc used to be invisible here. */
    if ((unsigned)buffer < 0x09000000u || (unsigned)buffer >= 0x10000000u) {
        return 1;
    }
    strcpy(buffer, "runtime");
    total = (int)strlen(buffer);
    total += atoi("40");
    for (i = 0; i < 3; ++i) {
        total += i;
    }
    {
        /* float is 64 bits on this target: one floating-point format, so a value
           neither narrows nor rounds on its way through the library. */
        float half = 0.5f;
        total += (int)(half * 10.0);
    }
    printf("total=%d float=%d text=%s\n", total, (int)sizeof(float), buffer);
    free(buffer);
    return 0;
}
