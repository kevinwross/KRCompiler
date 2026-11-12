#include <unistd.h>

int main(void) {
    int descriptor = open("kr32_dup_test.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (descriptor < 0) {
        return 1;
    }
    if (dup2(descriptor, 10) != 10) {
        return 2;
    }
    if (write(10, "z", 1) != 1) {
        return 3;
    }
    close(descriptor);
    close(10);

    descriptor = open("kr32_dup_test.txt", O_RDONLY);
    if (descriptor < 0) {
        return 4;
    }
    char value = 0;
    if (read(descriptor, &value, 1) != 1 || value != 'z') {
        return 5;
    }
    close(descriptor);
    unlink("kr32_dup_test.txt");
    return 0;
}
