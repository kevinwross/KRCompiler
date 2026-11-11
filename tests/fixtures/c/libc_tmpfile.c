#include <stdio.h>
#include <string.h>

int main(void) {
    FILE *stream = tmpfile();
    if (stream == 0) {
        return 1;
    }
    if (fputs("temporary", stream) != 0) {
        return 2;
    }
    if (fseek(stream, 0, 0) != 0) {
        return 3;
    }
    char buffer[16];
    if (fgets(buffer, sizeof(buffer), stream) == 0) {
        return 4;
    }
    fclose(stream);
    if (strcmp(buffer, "temporary") != 0) {
        return 5;
    }
    char name[L_tmpnam];
    if (tmpnam(name) == 0 || name[0] == 0) {
        return 6;
    }
    return 0;
}
