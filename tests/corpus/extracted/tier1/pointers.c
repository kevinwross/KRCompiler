/* Tier 1 — pointers. Arithmetic, indirection through a pointer to a pointer,
 * pointer-to-string walking, and the null comparison.
 *
 * Integer-only: KR16's IP16 profile rejects double and float. */

int main(void) {
    int values[5];
    int index = 0;
    while (index < 5) {
        values[index] = index * 10;
        index = index + 1;
    }

    /* Pointer arithmetic walks the array. */
    int* cursor = values;
    int total = 0;
    while (cursor < values + 5) {
        total = total + *cursor;
        cursor = cursor + 1;
    }
    if (total != 100) { return 1; }

    /* Indexing and dereference name the same object. */
    if (values[3] != 30) { return 2; }
    if (*(values + 3) != 30) { return 3; }
    if (*(cursor - 2) != 30) { return 4; }

    /* A pointer to a pointer, written through both. */
    int held = 5;
    int* first = &held;
    int** second = &first;
    **second = 9;
    if (held != 9) { return 5; }
    if (*first != 9) { return 6; }

    /* A string is walked by pointer until the terminator.  */
    char* message = "abc";
    int length = 0;
    while (*message != 0) {
        length = length + 1;
        message = message + 1;
    }
    if (length != 3) { return 7; }

    /* A null pointer compares equal to zero and unequal to a live object. */
    int* empty = 0;
    if (empty != 0) { return 8; }
    if (empty == &held) { return 9; }

    /* Pointer difference is measured in elements, not bytes. */
    int* begin = values;
    int* end = values + 5;
    if (end - begin != 5) { return 10; }

    return 0;
}
