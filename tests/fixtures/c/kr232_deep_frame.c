/* A frame object deeper than the eight-bit displacement: the array's base sits
   past 128 bytes, so its address has to be formed in R8, while its last element
   sits near FP and is reachable directly. The frame-layout check requires one
   object deeper than 128 bytes and every object to end at or below FP; the
   program returns 3 only when both accesses hit the right slots. */
int deep(int seed) {
    unsigned big[40];
    int tail;
    big[0] = (unsigned)seed;
    big[39] = (unsigned)seed + 1u;
    tail = (int)(big[0] + big[39]);
    return tail;
}

int main(void) {
    return deep(1);
}
