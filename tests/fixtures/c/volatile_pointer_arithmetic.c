/* A volatile access whose address is an arithmetic expression rather than a name.
 *
 * `*(base + 1)` must be marked volatile: the qualifier belongs to the pointee, and
 * adding an integer to a pointer does not change what it points at. The qualifier is
 * not recoverable from the expression's type -- the AST carries none and `ir::Type`
 * has no qualifier -- so the front end reads it from the declared pointer, through the
 * addition.
 *
 * The commuted form `*(1 + base)` is deliberately absent: it does not compile today,
 * because the address path types `1 + base` as an integer addition and then reports
 * "dereference of a non-pointer lvalue". That is a separate defect from the qualifier
 * and is recorded in implementation.md rather than fixed here. */

int read_second(volatile int *base) {
    return *(base + 1);
}

void write_second(volatile int *base, int value) {
    *(base + 1) = value;
}
