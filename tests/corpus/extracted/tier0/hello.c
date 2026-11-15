/* Extracted from ../kr32/examples at commit ba69940 for the Tier 0-1 corpus.
 * Copied verbatim; the original header, if any, follows. */
int putchar(int c);
void exit(int code);

int main(void) {
    char *message = "Hello, KR32!\n";
    while (*message) {
        putchar(*message);
        message = message + 1;
    }
    return 0;
}
