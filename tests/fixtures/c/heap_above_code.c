// A large allocation must not land on the program's own code. With the heap
// based at 0x00020000 and .text starting at 0x10000, a quarter-megabyte block
// overwrote the instructions that later ran. Returns 7 when the block reads
// back intact and the code after it still executes.
#include <stdlib.h>

static int after_the_heap(void) {
    return 7;
}

int main(void) {
    unsigned char *big = (unsigned char *)malloc(256u * 1024u);
    if (big == 0) {
        return 1;
    }
    for (unsigned int index = 0; index < 256u * 1024u; index++) {
        big[index] = (unsigned char)(index & 0xFF);
    }
    unsigned int wrong = 0;
    for (unsigned int index = 0; index < 256u * 1024u; index += 4096u) {
        if (big[index] != (unsigned char)(index & 0xFF)) {
            wrong++;
        }
    }
    if (wrong != 0) {
        return 2;
    }
    return after_the_heap();
}
