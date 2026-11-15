// The KR16 file layer end to end: open, write, read back, and compare.
//
// The image runs with --fs-root pointing at a dedicated directory and a quota
// large enough for it. The VM's filesystem quota defaults to 5 MiB and counts
// the whole root recursively, so pointing --fs-root at a build tree makes every
// write return SYS_ENOSPC -- which looks exactly like a broken layer, and did
// for three rounds.
//
// Only the stream functions are used: the link script provides the startup
// object, the host wrappers and the file layer, not the C library, so memset
// and strcmp are not available here.
//
// Returns 42.

#include <stdio.h>

static int same_text(const char* left, const char* right) {
    int index = 0;
    while (left[index] != 0 || right[index] != 0) {
        if (left[index] != right[index]) {
            return 0;
        }
        index++;
    }
    return 1;
}

int main() {
    FILE* file = fopen("layer.txt", "w");
    if (!file) {
        return 1;
    }
    if (fputs("hello layer\n", file) < 0) {
        return 2;
    }
    if (fclose(file) != 0) {
        return 3;
    }

    file = fopen("layer.txt", "r");
    if (!file) {
        return 4;
    }
    char buffer[16];
    int index = 0;
    while (index < 16) {
        buffer[index++] = 0;
    }
    if (!fgets(buffer, 16, file)) {
        return 5;
    }
    fclose(file);
    if (!same_text(buffer, "hello layer\n")) {
        return 6;
    }

    // A second open for writing truncates rather than appends.
    file = fopen("layer.txt", "w");
    if (!file) {
        return 7;
    }
    fputs("bye\n", file);
    fclose(file);
    file = fopen("layer.txt", "r");
    if (!file) {
        return 8;
    }
    index = 0;
    while (index < 16) {
        buffer[index++] = 0;
    }
    if (!fgets(buffer, 16, file)) {
        return 9;
    }
    fclose(file);
    if (!same_text(buffer, "bye\n")) {
        return 10;
    }
    return 42;
}