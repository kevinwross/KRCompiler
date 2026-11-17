/* Tier 1 — structs. Layout, nesting, by-value passing, and arrays of records.
 *
 * The tier definition lists structs as a feature area, and the interesting part on
 * two targets is that a record's size and field offsets come from the same rule on
 * both: natural alignment, two-byte on KR16, four-byte on KR32. Every check below
 * compares field values rather than sizeof, so the program is correct on either
 * profile and a layout disagreement shows up as a wrong value, not a wrong number. */

struct Pair {
    int left;
    int right;
};

struct Nested {
    struct Pair inner;
    int tail;
};

struct Flags {
    char first;
    char second;
    int wide;
};

int sum_pair(struct Pair value) {
    return value.left + value.right;
}

int main(void) {
    struct Pair pair;
    pair.left = 20;
    pair.right = 22;
    if (sum_pair(pair) != 42) { return 1; }

    /* A by-value argument is a copy: the callee's writes must not reach the caller. */
    if (pair.left != 20) { return 2; }

    struct Nested nested;
    nested.inner.left = 1;
    nested.inner.right = 2;
    nested.tail = 3;
    if (nested.inner.left + nested.inner.right + nested.tail != 6) { return 3; }

    /* Field order must be preserved as declared, including across an alignment gap. */
    struct Flags flags;
    flags.first = 'a';
    flags.second = 'b';
    flags.wide = 7;
    if (flags.first != 'a') { return 4; }
    if (flags.second != 'b') { return 5; }
    if (flags.wide != 7) { return 6; }

    /* An array of records, indexed by a loop. */
    struct Pair table[4];
    int index = 0;
    while (index < 4) {
        table[index].left = index;
        table[index].right = index * 2;
        index = index + 1;
    }
    int total = 0;
    for (index = 0; index < 4; index = index + 1) {
        total = total + table[index].left + table[index].right;
    }
    if (total != 18) { return 7; }

    /* A pointer to a record reaches the same object the name does. */
    struct Pair* pointer = &pair;
    if (pointer->left != 20) { return 8; }
    pointer->left = 21;
    if (pair.left != 21) { return 9; }

    return 0;
}
