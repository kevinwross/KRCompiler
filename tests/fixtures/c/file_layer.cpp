// The KR16 file layer's open and close. The image runs with --fs-root pointing
// at the build directory, so the VM's file services see a real root: without
// that flag the VM sandboxes to ./sandbox and every open fails, which reads
// exactly like a broken layer.
//
// Transfers are not asserted here. krc16_sys_write returns SYS_ENOSPC for every
// file descriptor while succeeding on descriptor 1, so the defect is the sibling
// VM's write service, not this layer; implementation.md records it.
//
// Returns 42. The exit status is the observable: the link script provides the
// startup object, the host wrappers and the file layer, not the C library, so
// this fixture calls nothing from <stdio.h> beyond the stream functions.

#include <stdio.h>

int main() {
    FILE* file = fopen("layer.txt", "w");
    if (!file) {
        return 1;
    }
    if (fclose(file) != 0) {
        return 2;
    }
    file = fopen("layer.txt", "w");
    if (!file) {
        return 3;
    }
    fclose(file);
    return 42;
}