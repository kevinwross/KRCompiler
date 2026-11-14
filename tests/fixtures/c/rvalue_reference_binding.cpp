// A parameter declared with a reference binds to the object its argument names,
// including when the argument is a cast to a reference: a write through the
// parameter reaches the caller's object. Before this, `static_cast<T&&>(x)`
// passed to a `T&&` parameter was copied into a temporary and the write was
// lost. Returns 42.

struct S {
    int value;
    S() : value(0) {}
};

void zero(S&& target) { target.value = 0; }
void set(S& target, int value) { target.value = value; }
void zero_through_const(const S& target, int* seen) { *seen = target.value; }

int main() {
    S first;
    first.value = 7;

    // A cast to an rvalue reference names the object, so the write lands.
    zero(static_cast<S&&>(first));
    if (first.value != 0) {
        return 1;
    }

    // The plain reference form still works.
    set(first, 9);
    if (first.value != 9) {
        return 2;
    }

    // A cast to a const reference also names the object; the write is refused by
    // Sema, so this reads instead.
    int seen = -1;
    zero_through_const(static_cast<const S&>(first), &seen);
    if (seen != 9) {
        return 3;
    }

    // A temporary still outlives the call that binds it.
    S temporary;
    temporary.value = 5;
    zero(static_cast<S&&>(temporary));
    if (temporary.value != 0) {
        return 4;
    }
    return 42;
}
