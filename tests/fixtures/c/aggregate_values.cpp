// An aggregate read out of an object is a value like any other, so it can be
// passed by value, copied, and read through a member, an array element, a
// pointer dereference, or a global. Returns 42 when every read is intact.

struct Large {
    int a;
    int b;
    int c;
    int d;
    int e;
};

struct Holder {
    Large value;
    int factor;
};

struct Nested {
    Holder holder;
    int tag;
};

Large global_large;

static int sum(Large value) {
    return value.a + value.b + value.c + value.d + value.e;
}

static Large make(int base) {
    Large value;
    value.a = base;
    value.b = base + 1;
    value.c = base + 2;
    value.d = base + 3;
    value.e = base + 4;
    return value;
}

int main() {
    Holder holder;
    holder.value = make(1);
    holder.factor = 2;
    if (sum(holder.value) != 15) {
        return 1;
    }

    // A copy of the member is a separate object.
    Large copy = holder.value;
    copy.a = 100;
    if (sum(holder.value) != 15 || sum(copy) != 114) {
        return 2;
    }

    // Array elements of aggregate type read as values.
    Large array[2];
    array[0] = make(10);
    array[1] = make(20);
    if (sum(array[0]) != 60 || sum(array[1]) != 110) {
        return 3;
    }

    // A pointer dereference of an aggregate reads as a value.
    Large* pointer = &array[1];
    if (sum(*pointer) != 110) {
        return 4;
    }

    // A global aggregate reads as a value, and a write through a member
    // reaches the object.
    global_large = make(30);
    if (sum(global_large) != 160) {
        return 5;
    }
    global_large.a = 7;
    if (sum(global_large) != 137) {
        return 6;
    }

    // A member of a member reads as a value too.
    Nested nested;
    nested.holder = holder;
    nested.tag = 9;
    if (sum(nested.holder.value) != 15 || nested.tag != 9) {
        return 7;
    }

    // The value can be passed straight on without a named temporary.
    if (sum(nested.holder.value) + sum(array[0]) != 75) {
        return 8;
    }

    return 42;
}
