// Exception handling over the `.cpp_except` descriptors and the runtime
// unwinder (ADR 0026): a throw in a callee reaches a handler in a caller, a
// base-class handler catches a derived exception, a catch-all sees the
// exception, and a rethrow leaves the inner handler for the outer one.
// Returns 42 when every check holds.

struct Base {
    virtual int kind() const { return 1; }
    int code;
};

struct Derived : Base {
    int kind() const override { return 2; }
};

static void throw_base(int code) {
    Base value;
    value.code = code;
    throw value;
}

static void throw_derived(int code) {
    Derived value;
    value.code = code;
    throw value;
}

// Two frames between the throw and the handler.
static void throw_deep(int code) {
    throw_derived(code);
}

static int nested(int code) {
    try {
        try {
            throw_derived(code);
        } catch (Base& value) {
            // Not the handler for this type: pass the exception outward.
            throw;
        }
    } catch (Derived& value) {
        return value.code;
    }
    return 0;
}

int main() {
    // A handler in the caller catches a throw from a callee two frames down.
    int caught = 0;
    try {
        throw_deep(4);
    } catch (Derived& value) {
        caught = value.code;
    }
    if (caught != 4) {
        return 1;
    }

    // A base-class handler catches a derived exception.
    int base_catch = 0;
    try {
        throw_derived(5);
    } catch (Base& value) {
        base_catch = value.kind() + value.code;
    }
    if (base_catch != 7) {
        return 2;
    }

    // A matched handler is selected over the base-class one when it comes
    // first.
    int first = 0;
    try {
        throw_derived(6);
    } catch (Derived& value) {
        first = 1;
    } catch (Base& value) {
        first = 2;
    }
    if (first != 1) {
        return 3;
    }

    // A catch-all sees an exception no typed handler matched.
    int catch_all = 0;
    try {
        throw_base(3);
    } catch (...) {
        catch_all = 1;
    }
    if (catch_all != 1) {
        return 4;
    }

    // Rethrow from a handler reaches the enclosing try.
    if (nested(7) != 7) {
        return 5;
    }

    // An exception thrown inside a handler is caught by its enclosing try.
    int from_handler = 0;
    try {
        try {
            throw_base(1);
        } catch (Base& value) {
            (void)value;
            throw_derived(9);
        }
    } catch (Derived& value) {
        from_handler = value.code;
    }
    if (from_handler != 9) {
        return 6;
    }

    return 42;
}
