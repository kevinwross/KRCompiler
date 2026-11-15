/* Extracted from ../kr32/examples at commit ba69940 for the Tier 1-2 corpus.
 * Copied verbatim; the original header, if any, follows. */
// Show the host's native open-file dialog and print the selected path.

#include <stdio.h>
#include <stdlib.h>

int main() {
    char path[4096];
    const int length = sys_open_dialog(path, (int)sizeof(path));
    if (length == -1) {
        printf("FileOpen: canceled\n");
        return 1;
    }
    if (length == -2) {
        printf("FileOpen: file dialog unavailable\n");
        return 2;
    }
    if (length < 0) {
        printf("FileOpen: dialog failed (%d)\n", length);
        return 3;
    }
    printf("%s\n", path);
    return 0;
}
