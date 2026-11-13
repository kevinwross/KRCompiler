// Destructor cleanup on propagation (ADR 0027): every lexical scope that owns
// a destructible automatic object gets a cleanup landing pad, and the pads
// chain outward through the frame. The guard records the order in which
// destructors ran. Returns 42 when every check holds.

struct Payload {
    int value;
};

static int destroyed = 0;

struct Guard {
    int order;
    explicit Guard(int which) { order = which; }
    ~Guard() { destroyed = destroyed * 10 + order; }
};

static void throw_with_guard(int which) {
    Guard guard(which);
    Payload payload;
    payload.value = which;
    throw payload;
}

static int catch_in_callee() {
    try {
        throw_with_guard(1);
    } catch (Payload& payload) {
        return payload.value;
    }
    return 0;
}

int main() {
    // The guard inside the throwing function runs before the handler.
    destroyed = 0;
    int caught = 0;
    try {
        throw_with_guard(4);
    } catch (Payload& payload) {
        caught = payload.value;
    }
    if (caught != 4 || destroyed != 4) {
        return 1;
    }

    // Two guards in one scope are destroyed in reverse construction order.
    destroyed = 0;
    try {
        Guard first(1);
        Guard second(2);
        Payload payload;
        payload.value = 7;
        throw payload;
    } catch (Payload& payload) {
        caught = payload.value;
    }
    if (caught != 7 || destroyed != 21) {
        return 2;
    }

    // A nested scope's guard is destroyed there and the outer guard after it.
    destroyed = 0;
    try {
        Guard outer(5);
        {
            Guard inner(6);
            Payload payload;
            payload.value = 8;
            throw payload;
        }
    } catch (Payload& payload) {
        caught = payload.value;
    }
    if (caught != 8 || destroyed != 65) {
        return 3;
    }

    // A guard constructed after the throw point is not destroyed.
    destroyed = 0;
    try {
        throw_with_guard(3);
    } catch (Payload& payload) {
        caught = payload.value;
    }
    if (caught != 3 || destroyed != 3) {
        return 4;
    }

    // Cleanup also runs when the throw crosses a function boundary.
    destroyed = 0;
    if (catch_in_callee() != 1 || destroyed != 1) {
        return 5;
    }

    return 42;
}
