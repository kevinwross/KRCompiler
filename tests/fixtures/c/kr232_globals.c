/* An array global followed by two counters. The array was emitted as four bytes
   in .data, so the linker placed the counters inside it: a write to block[2]
   changed a counter, and a program that read two status bytes in one function
   returned 15 where it meant 0. Exit 12 means both counters kept their own
   address. */
static unsigned block[6];
static int first;
static int second;

int main(void) {
    block[2] = 15;
    first = 1;
    second = 2;
    return first * 10 + second;
}
