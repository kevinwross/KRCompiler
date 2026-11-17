/* Tier 1 — control flow. if/else chains, while, do-while, for, switch with
 * fallthrough, break, continue, and the short-circuit operators.
 *
 * Integer-only, and no goto: this tier is about the statements a program is built
 * from, and a goto would make a failure harder to attribute. */

int classify(int value) {
    if (value < 0) {
        return 1;
    } else if (value == 0) {
        return 2;
    }
    return 3;
}

int main(void) {
    /* if / else if / else, including the equality boundary. */
    if (classify(-5) != 1) { return 1; }
    if (classify(0) != 2) { return 2; }
    if (classify(5) != 3) { return 3; }

    /* while with a compound condition. */
    int counter = 0;
    int guard = 0;
    while (counter < 10 && guard < 100) {
        counter = counter + 1;
        guard = guard + 1;
    }
    if (counter != 10) { return 4; }

    /* do-while runs its body once even when the condition starts false. */
    int once = 0;
    do {
        once = once + 1;
    } while (0);
    if (once != 1) { return 5; }

    /* for with a declaration in the initialiser, and continue skipping an element. */
    int evens = 0;
    for (int index = 0; index < 10; index = index + 1) {
        if (index % 2 != 0) {
            continue;
        }
        evens = evens + 1;
    }
    if (evens != 5) { return 6; }

    /* break leaves only the innermost loop. */
    int inner = 0;
    int outer = 0;
    for (outer = 0; outer < 3; outer = outer + 1) {
        for (int index = 0; index < 10; index = index + 1) {
            if (index == 2) {
                break;
            }
            inner = inner + 1;
        }
    }
    if (inner != 6) { return 7; }

    /* switch with fallthrough, a default, and break. */
    int fallen = 0;
    switch (2) {
    case 1:
        fallen = fallen + 1;
        /* fall through on purpose */
    case 2:
        fallen = fallen + 10;
        /* fall through on purpose */
    case 3:
        fallen = fallen + 100;
        break;
    default:
        fallen = fallen + 1000;
        break;
    }
    if (fallen != 110) { return 8; }

    /* A switch whose value matches no case takes default. */
    int unmatched = 0;
    switch (9) {
    case 1:
        unmatched = 1;
        break;
    default:
        unmatched = 2;
        break;
    }
    if (unmatched != 2) { return 9; }

    /* Short-circuit: the right operand must not be evaluated when the left decides. */
    int evaluations = 0;
    if (0 && (evaluations = evaluations + 1)) {
        return 10;
    }
    if (evaluations != 0) { return 11; }
    if (!(1 || (evaluations = evaluations + 1))) {
        return 12;
    }
    if (evaluations != 0) { return 13; }

    /* The conditional operator, in both branches. */
    int chosen = (counter > 5) ? 42 : 0;
    if (chosen != 42) { return 14; }

    return 0;
}
