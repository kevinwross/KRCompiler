/* Extracted from ../kr32/examples at commit ba69940 for the Tier 0-1 corpus.
 * Copied verbatim; the original header, if any, follows. */
// KR32 libc string and memory routine demonstration.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    char text[64];
    char padded[8];
    char overlap[16];
    char tokens[32];

    strcpy(text, "hello");
    strcat(text, " world");
    printf("strcpy/strcat: %s\n", text);

    strncpy(padded, "xy", 5);
    printf("strncpy padding: %d %d %d\n",
           padded[2], padded[3], padded[4]);

    strncat(text, "!!!", 2);
    printf("strncat: %s\n", text);

    printf("strcmp: %d %d\n",
           strcmp("abc", "abc"), strcmp("abc", "abd"));
    printf("strncmp: %d\n", strncmp("abcx", "abcy", 3));

    char *first = strchr(text, 'o');
    char *last = strrchr(text, 'o');
    printf("strchr/strrchr: %d %d\n", first != 0, first != last);

    char *found = strstr(text, "world");
    if (found == 0) {
        printf("strstr: missing\n");
    } else {
        printf("strstr: %s\n", found);
    }

    strcpy(overlap, "abcdef");
    memmove(overlap + 2, overlap, 4);
    printf("memmove: %s\n", overlap);
    printf("memcmp: %d %d\n",
           memcmp("abc", "abc", 3), memcmp("abc", "abd", 3));
    printf("memchr: %d\n", memchr(overlap, 'd', 16) != 0);

    char *duplicate = strdup("copied text");
    printf("strdup: %s\n", duplicate);
    free(duplicate);

    strcpy(tokens, "one,two;three");
    char *token = strtok(tokens, ",;");
    while (token != 0) {
        printf("token=%s\n", token);
        token = strtok(0, ",;");
    }
    return 0;
}
