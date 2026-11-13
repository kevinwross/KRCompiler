// The KR16 heap is a first-fit free list over the region the linker reports:
// every block carries its payload size, a forward link, and a free flag, and a
// freed block merges forward with the next free one. Returns 42 when reuse,
// splitting, merging, and realloc all behave.

void *malloc(unsigned int size);
void free(void *pointer);
void *calloc(unsigned int count, unsigned int size);
void *realloc(void *pointer, unsigned int size);

int main(void) {
    // A basic allocation is writable.
    unsigned char *first = (unsigned char *)malloc(8);
    if (first == 0) {
        return 1;
    }
    first[0] = 11;
    first[7] = 22;
    if (first[0] != 11 || first[7] != 22) {
        return 2;
    }

    // Freeing and asking for the same size reuses the block.
    free(first);
    unsigned char *again = (unsigned char *)malloc(8);
    if (again != first) {
        return 3;
    }
    free(again);

    // Splitting: a large block serves two smaller ones.
    unsigned char *whole = (unsigned char *)malloc(32);
    if (whole == 0) {
        return 4;
    }
    free(whole);
    unsigned char *left = (unsigned char *)malloc(8);
    unsigned char *right = (unsigned char *)malloc(8);
    if (left == 0 || right == 0 || left == right) {
        return 5;
    }
    if ((unsigned int)right <= (unsigned int)left) {
        return 6;
    }
    free(left);
    free(right);

    // Merging: two freed neighbours serve one allocation that neither could.
    unsigned char *one = (unsigned char *)malloc(16);
    unsigned char *two = (unsigned char *)malloc(16);
    if (one == 0 || two == 0) {
        return 7;
    }
    free(one);
    free(two);
    unsigned char *merged = (unsigned char *)malloc(32);
    if (merged == 0) {
        return 8;
    }
    free(merged);

    // calloc zeroes and detects an overflowing product.
    unsigned char *zeros = (unsigned char *)calloc(4, 4);
    if (zeros == 0) {
        return 9;
    }
    unsigned int index = 0;
    while (index < 16) {
        if (zeros[index] != 0) {
            return 10;
        }
        index++;
    }
    free(zeros);
    if (calloc(0x8000u, 4u) != 0) {
        return 11;
    }

    // realloc keeps the contents it already had and releases the old block.
    unsigned char *grown = (unsigned char *)malloc(8);
    if (grown == 0) {
        return 12;
    }
    index = 0;
    while (index < 8) {
        grown[index] = (unsigned char)(index + 1);
        index++;
    }
    unsigned char *bigger = (unsigned char *)realloc(grown, 24);
    if (bigger == 0) {
        return 13;
    }
    index = 0;
    while (index < 8) {
        if (bigger[index] != (unsigned char)(index + 1)) {
            return 14;
        }
        index++;
    }
    // The grown block has room for the larger payload.
    bigger[23] = 99;
    if (bigger[23] != 99) {
        return 15;
    }
    free(bigger);

    // Shrinking realloc keeps the prefix.
    unsigned char *shrunk = (unsigned char *)realloc(0, 16);
    if (shrunk == 0) {
        return 16;
    }
    shrunk[0] = 1;
    shrunk[1] = 2;
    unsigned char *smaller = (unsigned char *)realloc(shrunk, 4);
    if (smaller == 0 || smaller[0] != 1 || smaller[1] != 2) {
        return 17;
    }
    free(smaller);

    // Exhausting the heap reports failure, and releasing everything makes a
    // large allocation possible again.
    void *blocks[512];
    int count = 0;
    while (count < 512) {
        blocks[count] = malloc(512);
        if (blocks[count] == 0) {
            break;
        }
        count++;
    }
    if (count == 512) {
        return 18;
    }
    if (count == 0) {
        return 19;
    }
    int released = 0;
    while (released < count) {
        free(blocks[released]);
        released++;
    }
    unsigned char *large = (unsigned char *)malloc(1024);
    if (large == 0) {
        return 20;
    }
    large[1023] = 7;
    if (large[1023] != 7) {
        return 21;
    }
    free(large);

    // free(0) is a no-op.
    free(0);

    return 42;
}
