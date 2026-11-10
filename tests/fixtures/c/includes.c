#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char *text = malloc(16);
    if (text == 0) {
        return 1;
    }
    strcpy(text, "KR32");
    printf("%s %d\n", text, 3);
    free(text);
    return 0;
}
