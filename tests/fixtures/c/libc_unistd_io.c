#include <unistd.h>

int main(void) {
    int descriptor = open("kr32_unistd_test.txt", O_WRONLY | O_CREAT | O_TRUNC);
    if (descriptor < 0) {
        return 1;
    }
    if (write(descriptor, "abc", 3) != 3) {
        return 2;
    }
    close(descriptor);

    descriptor = open("kr32_unistd_test.txt", O_RDONLY);
    if (descriptor < 0) {
        return 3;
    }
    char buffer[4];
    if (read(descriptor, buffer, 3) != 3) {
        return 4;
    }
    close(descriptor);
    unlink("kr32_unistd_test.txt");
    if (buffer[0] != 'a' || buffer[1] != 'b' || buffer[2] != 'c') {
        return 5;
    }
    return 0;
}
