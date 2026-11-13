// Aggregates larger than eight bytes are memory class: the caller passes a
// pointer to a copy it owns and the callee works on its own copy (docs/abi.md).
// Returns 42 when the values survive the trip, the callee's writes stay in its
// copy, and a large aggregate can be returned and passed on again.

struct Large {
    int a;
    int b;
    int c;
    int d;
    int e;
};

struct Mixed {
    double weight;
    int count;
};

struct Wide {
    double x;
    double y;
    double z;
};

struct Holder {
    Large value;
    int factor;
};

static int sum(Large value) {
    return value.a + value.b + value.c + value.d + value.e;
}

static void clobber(Large value) {
    value.a = 100;
    value.b = 200;
}

static Large make_large(int base) {
    Large value;
    value.a = base;
    value.b = base + 1;
    value.c = base + 2;
    value.d = base + 3;
    value.e = base + 4;
    return value;
}

static Large pass_through(Large value) {
    return value;
}

static int sum_mixed(Mixed value) {
    return static_cast<int>(value.weight) + value.count;
}

static double sum_wide(Wide value) {
    return value.x + value.y + value.z;
}

struct Consumer {
    int total;
    explicit Consumer(Large value) { total = sum(value); }
    int add(Large value) const { return total + sum(value); }
};

int main() {
    Large large;
    large.a = 1;
    large.b = 2;
    large.c = 3;
    large.d = 4;
    large.e = 5;

    if (sum(large) != 15) {
        return 1;
    }

    // The callee writes to its own copy, so the caller's object is unchanged.
    clobber(large);
    if (large.a != 1 || large.b != 2) {
        return 2;
    }

    // A large aggregate can be returned by value and passed on again.
    Large made = make_large(10);
    if (sum(made) != 60) {
        return 3;
    }
    if (sum(pass_through(made)) != 60) {
        return 4;
    }

    // A twelve-byte aggregate with an FP member is memory class too.
    Mixed mixed;
    mixed.weight = 4.0;
    mixed.count = 2;
    if (sum_mixed(mixed) != 6) {
        return 5;
    }

    // An FP-only aggregate above sixteen bytes is memory class.
    Wide wide;
    wide.x = 1.5;
    wide.y = 2.5;
    wide.z = 3.0;
    if (sum_wide(wide) != 7.0) {
        return 6;
    }

    // Constructors and methods take the same path.
    Consumer consumer(large);
    if (consumer.total != 15) {
        return 7;
    }
    if (consumer.add(made) != 75) {
        return 8;
    }

    // An aggregate nested in another aggregate survives a copy.
    Holder holder;
    holder.value = made;
    holder.factor = 2;
    const int nested_total = holder.value.a + holder.value.b + holder.value.c +
                             holder.value.d + holder.value.e;
    if (nested_total * holder.factor != 120) {
        return 9;
    }

    return 42;
}
