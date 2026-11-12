#include <stdio.h>
#include <unistd.h>

int main(void) {
    int descriptor = open("kr32_dprintf_test.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (descriptor < 0) {
        return 1;
    }
    if (dprintf(descriptor, "%d %s", 42, "ok") <= 0) {
        return 2;
    }
    close(descriptor);

    descriptor = open("kr32_dprintf_test.txt", O_RDONLY);
    if (descriptor < 0) {
        return 3;
    }
    char buffer[16];
    const int length = read(descriptor, buffer, sizeof(buffer) - 1);
    close(descriptor);
    unlink("kr32_dprintf_test.txt");
    if (length != 5) {
        return 4;
    }
    buffer[length] = 0;
    if (buffer[0] != '4' || buffer[1] != '2' || buffer[3] != 'o') {
        return 5;
    }
    return 0;
}
