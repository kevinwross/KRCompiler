unsigned int strlen(const char *text) {
    unsigned int length = 0;
    while (text[length] != 0) {
        length++;
    }
    return length;
}

int strcmp(const char *left, const char *right) {
    while (*left != 0 && *left == *right) {
        left++;
        right++;
    }
    return (int)*left - (int)*right;
}

int strncmp(const char *left, const char *right, unsigned int count) {
    while (count > 0 && *left != 0 && *left == *right) {
        left++;
        right++;
        count--;
    }
    if (count == 0) {
        return 0;
    }
    return (int)*left - (int)*right;
}

char *strcpy(char *destination, const char *source) {
    char *result = destination;
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

char *strcat(char *destination, const char *source) {
    char *result = destination;
    while (*destination != 0) {
        destination++;
    }
    while ((*destination++ = *source++) != 0) {
    }
    return result;
}

void *memcpy(void *destination, const void *source, unsigned int count) {
    unsigned char *out = (unsigned char *)destination;
    const unsigned char *in = (const unsigned char *)source;
    while (count > 0) {
        *out++ = *in++;
        count--;
    }
    return destination;
}

void *memset(void *destination, int value, unsigned int count) {
    unsigned char *out = (unsigned char *)destination;
    while (count > 0) {
        *out++ = (unsigned char)value;
        count--;
    }
    return destination;
}

int memcmp(const void *left, const void *right, unsigned int count) {
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (count > 0 && *a == *b) {
        a++;
        b++;
        count--;
    }
    if (count == 0) {
        return 0;
    }
    return (int)*a - (int)*b;
}
