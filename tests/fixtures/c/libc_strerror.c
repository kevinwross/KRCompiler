#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    errno = 0;
    if (strcmp(strerror(ENOENT), "No such file or directory") != 0) {
        return 1;
    }
    if (strcmp(strerror(EINVAL), "Invalid argument") != 0) {
        return 2;
    }
    if (strcmp(strerror(999), "Unknown error") != 0) {
        return 3;
    }
    errno = ENOENT;
    perror("open");
    return 0;
}
