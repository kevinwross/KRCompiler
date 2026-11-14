// `&&` and `||` short-circuit: the right operand is evaluated only when the
// left one does not decide the result, and the operator still yields the right
// value. Returns 42 when every count and result holds.

int calls = 0;

static int zero(void) {
    calls = calls + 1;
    return 0;
}

static int one(void) {
    calls = calls + 1;
    return 1;
}

int main(void) {
    // A false left operand of `&&` skips the right one.
    calls = 0;
    if (zero() && one()) {
        return 1;
    }
    if (calls != 1) {
        return 2;
    }

    // A true left operand of `||` skips the right one.
    calls = 0;
    if (!(one() || zero())) {
        return 3;
    }
    if (calls != 1) {
        return 4;
    }

    // A true left operand of `&&` evaluates the right one.
    calls = 0;
    if (!(one() && one())) {
        return 5;
    }
    if (calls != 2) {
        return 6;
    }

    // A false left operand of `||` evaluates the right one.
    calls = 0;
    if (!(zero() || one())) {
        return 7;
    }
    if (calls != 2) {
        return 8;
    }

    // The operator's value is the deciding operand's value, used as a value.
    calls = 0;
    int skipped = zero() && one();
    if (skipped != 0 || calls != 1) {
        return 9;
    }
    calls = 0;
    int taken = one() || zero();
    if (taken != 1 || calls != 1) {
        return 10;
    }

    // Chained operators nest and still short-circuit.
    calls = 0;
    if (zero() && one() && one()) {
        return 11;
    }
    if (calls != 1) {
        return 12;
    }
    calls = 0;
    if (!(one() || zero() || zero())) {
        return 13;
    }
    if (calls != 1) {
        return 14;
    }

    // A loop whose condition short-circuits stops asking at the first miss: the
    // third iteration's check passes, the fourth fails before `one()` runs, so
    // `one` is called three times.
    calls = 0;
    int index = 0;
    while (index < 3 && one()) {
        index = index + 1;
    }
    if (index != 3 || calls != 3) {
        return 15;
    }

    return 42;
}
