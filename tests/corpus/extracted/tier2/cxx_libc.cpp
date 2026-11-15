/* Extracted from ../kr32/examples at commit ba69940 for the Tier 1-2 corpus.
 * Copied verbatim; the original header, if any, follows. */
#include <stdio.h>
#include <string.h>

int main() {
    char text[16];
    strcpy(text, "shared libc");
    printf("C++ via C libc: %s\n", text);
    return 0;
}
