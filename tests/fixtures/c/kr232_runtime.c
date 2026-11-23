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
    printf("total=%d text=%s\n", total, buffer);
    free(buffer);
    return 0;
}
