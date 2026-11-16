// Array new over a sub-word record. The allocation is sized from the record's
// layout and each element is constructed at a stride from the same layout, so if
// the two disagreed the second element would be written past the allocation. That
// is exactly what happened while the expression path padded every field to four
// bytes and the back end did not. Returns 42.

struct Three { char a; char b; char c; };
struct Nested { struct Three inner; int tail; };

int main() {
    Three* items = new Three[3];
    items[0].a = 'a';
    items[1].b = 'b';
    items[2].c = 'c';
    if (items[0].a != 'a') { return 1; }
    if (items[1].b != 'b') { return 2; }
    if (items[2].c != 'c') { return 3; }

    // The same layout through a nested record, written and read back.
    Nested nested;
    nested.inner.a = 'x';
    nested.inner.c = 'z';
    nested.tail = 7;
    if (nested.inner.a != 'x') { return 4; }
    if (nested.inner.c != 'z') { return 5; }
    if (nested.tail != 7) { return 6; }

    delete[] items;
    return 42;
}
