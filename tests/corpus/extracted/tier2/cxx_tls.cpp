/* Extracted from ../kr32/examples at commit ba69940 for the Tier 1-2 corpus.
 * Copied verbatim; the original header, if any, follows. */
int seed() { return 42; }

thread_local int value = seed();
thread_local long long wide = seed() * 1000000000LL;

int main() {
    return value + (int)(wide / 1000000000) - 42;
}
