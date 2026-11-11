#include <krc/host.h>
#include <errno.h>

int main(void) {
    const char *path = "kr32_fileio_test.txt";
    const char *renamed = "kr32_fileio_renamed.txt";
    unlink(path);
    unlink(renamed);
    errno = 0;
    if (open("kr32_missing_file.txt", KRC_OPEN_READ) != -1 || errno == 0) {
        return 6;
    }
    int descriptor = open(path, KRC_OPEN_READ_WRITE | KRC_OPEN_CREATE |
                                  KRC_OPEN_TRUNCATE);
    if (descriptor < 0) {
        return 1;
    }
    const char text[] = "hello";
    if (write(descriptor, text, 5) != 5) {
        return 2;
    }
    if (close(descriptor) != 0) {
        return 3;
    }
    if (rename(path, renamed) != 0) {
        return 4;
    }
    if (unlink(renamed) != 0) {
        return 5;
    }
    return 0;
}
