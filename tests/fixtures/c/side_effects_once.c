/* A call used in an expression runs exactly once, in every position the
   language allows. Each check resets the counter, performs one expression, and
   asserts the callee ran once: a duplicated evaluation shows up as two.
   Returns 42. */

static int calls;

static int bump(void) {
    calls = calls + 1;
    return calls;
}

static int takes(int value) { return value; }

static int table[4];

int main(void) {
    /* An initializer. */
    calls = 0;
    int first = bump();
    if (calls != 1 || first != 1) {
        return 1;
    }

    /* A later assignment. */
    calls = 0;
    int second = 0;
    second = bump();
    if (calls != 1 || second != 1) {
        return 2;
    }

    /* A call argument. */
    calls = 0;
    int third = takes(bump());
    if (calls != 1 || third != 1) {
        return 3;
    }

    /* An operand of a binary expression. */
    calls = 0;
    int fourth = 1 + bump();
    if (calls != 1 || fourth != 2) {
        return 4;
    }

    /* A condition. */
    calls = 0;
    if (bump() == 1) {
        if (calls != 1) {
            return 5;
        }
    } else {
        return 6;
    }

    /* A subscript. */
    calls = 0;
    table[0] = 7;
    table[1] = 8;
    int fifth = table[bump()];
    if (calls != 1 || fifth != 8) {
        return 7;
    }

    /* A cast. */
    calls = 0;
    int sixth = (int)bump();
    if (calls != 1 || sixth != 1) {
        return 8;
    }

    /* A compound assignment. */
    calls = 0;
    int seventh = 0;
    seventh += bump();
    if (calls != 1 || seventh != 1) {
        return 9;
    }

    /* The left operand of a logical and. */
    calls = 0;
    if (bump() == 1 && 1) {
        if (calls != 1) {
            return 10;
        }
    } else {
        return 11;
    }

    /* The right operand of a logical or, which runs because the left is false. */
    calls = 0;
    if (0 || bump() == 1) {
        if (calls != 1) {
            return 12;
        }
    } else {
        return 13;
    }

    /* A conditional expression's condition. */
    calls = 0;
    int eighth = bump() == 1 ? 3 : 4;
    if (calls != 1 || eighth != 3) {
        return 14;
    }

    /* A loop condition: one call per test, so the loop runs once. */
    calls = 0;
    int ninth = 0;
    while (bump() < 2) {
        ninth = ninth + 1;
    }
    if (calls != 2 || ninth != 1) {
        return 15;
    }

    /* The increment of a for statement. */
    calls = 0;
    int tenth = 0;
    for (int index = 0; index < 2; index = index + bump()) {
        tenth = tenth + 1;
    }
    if (calls != 2 || tenth != 2) {
        return 16;
    }

    /* A returned call. */
    calls = 0;
    if (takes(bump()) != 1) {
        return 17;
    }
    if (calls != 1) {
        return 18;
    }

    /* A call whose result is compared again after use holds its value. */
    calls = 0;
    int eleventh = bump();
    if (calls != 1 || eleventh != 1) {
        return 19;
    }
    return 42;
}
