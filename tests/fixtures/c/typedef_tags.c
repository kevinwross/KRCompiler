// A tagged type defined inside its own typedef is a single definition, not a
// redefinition, and a self-referential member is how a list is written.
// Returns 42 when the typedef forms and size-based pointer arithmetic work.

typedef struct Node {
    struct Node *next;
    int value;
} Node;

typedef enum Color {
    Red,
    Green,
    Blue
} Color;

typedef union Number {
    int whole;
    unsigned int bits;
} Number;

int main(void) {
    Node first;
    Node second;
    first.next = &second;
    first.value = 10;
    second.next = 0;
    second.value = 32;

    if (first.value + first.next->value != 42) {
        return 1;
    }

    Color color = Blue;
    if ((int)color != 2) {
        return 2;
    }

    Number number;
    number.whole = 7;
    if (number.bits != 7u) {
        return 3;
    }

    // A backward offset computed from sizeof scales by the element size.
    int values[4];
    values[0] = 11;
    values[1] = 22;
    values[2] = 33;
    values[3] = 44;
    int *end = values + 4;
    int *previous = end - sizeof(int) / sizeof(int);
    if (*previous != 44) {
        return 4;
    }
    unsigned char *bytes = (unsigned char *)values;
    unsigned char *offset = bytes - sizeof(int);
    if ((unsigned int)offset + sizeof(int) != (unsigned int)bytes) {
        return 5;
    }

    return 42;
}
