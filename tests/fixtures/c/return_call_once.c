/* A call in a return statement runs once. The forwarding wrapper below is the
   shape that exposed the defect: `return bump();` used to emit the call twice,
   so bump incremented twice and the wrapper returned the second value.
   Returns 42. */

static int counter;
static int calls;

static int bump(void) {
    counter = counter + 1;
    return counter;
}

/* A thin forwarder: the call in the return statement must run exactly once. */
static int forward(void) { return bump(); }

/* The same shape with an argument that the callee uses. */
static int twice(int value) { return value + value; }
static int forward_with_argument(int value) { return twice(value); }

int main(void) {
    int value = forward();
    if (value != 1) {
        return 1;
    }
    if (counter != 1) {
        return 2;
    }

    /* A second forwarder call continues from the counter, one step at a time. */
    if (forward() != 2) {
        return 3;
    }
    if (counter != 2) {
        return 4;
    }

    if (forward_with_argument(21) != 42) {
        return 5;
    }

    /* A returned conversion call also runs once: the operator is the callee. */
    calls = 0;
    if (forward() != 3) {
        return 6;
    }
    if (calls != 0) {
        return 7;
    }
    return 42;
}
