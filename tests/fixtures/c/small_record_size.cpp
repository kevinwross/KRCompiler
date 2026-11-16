// `sizeof(<expression>)` must equal `sizeof(<type>)` for the same record.
//
// The expression path used to pad every field to four bytes, so the three-char
// record below measured twelve from an expression and three from its type -- and
// the same number decided whether a by-value aggregate was passed in registers.
// Both paths now use the record's own field alignment, which is also what the
// target's back end lays out. Returns 42.

struct Three { char a; char b; char c; };
struct Nested { struct Three inner; int tail; };

int main() {
    struct Three three;
    struct Nested nested;
    three.a = 1;
    three.b = 2;
    three.c = 3;
    nested.inner.b = 7;
    nested.tail = 9;

    // The identifier and expression paths against the type path.
    if (sizeof(three) != sizeof(struct Three)) { return 1; }
    if (sizeof(nested.inner) != sizeof(struct Three)) { return 2; }
    if (sizeof(nested) != sizeof(struct Nested)) { return 3; }

    // The fields still land where the layout says they do.
    if (three.b != 2) { return 4; }
    if (nested.inner.b != 7) { return 5; }
    if (nested.tail != 9) { return 6; }
    return 42;
}
