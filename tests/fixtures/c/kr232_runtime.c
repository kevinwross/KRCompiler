#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *buffer = (char *)malloc(32);
    int total = 0;
    int i;
    if (buffer == 0) {
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
