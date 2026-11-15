/* Extracted from ../kr32/examples at commit ba69940 for the Tier 0-1 corpus.
 * Copied verbatim; the original header, if any, follows. */
int printf(const char *format, ...);

int main(int argc, char *argv[]) {
    printf("argc = %d\n", argc);
    for (int index = 0; index < argc; ++index) {
        printf("argv[%d] = %s\n", index, argv[index]);
    }
    return 0;
}
