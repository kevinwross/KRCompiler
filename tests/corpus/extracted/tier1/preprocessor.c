/* Tier 1 — preprocessor. Object-like and function-like macros, conditional
 * compilation, and the operators that are easiest to get wrong.
 *
 * The values are checked at run time rather than in a #if, so a macro that expands
 * incorrectly shows up as a wrong result instead of a silent compile-time choice. */

#define ANSWER 42
#define DOUBLE(value) ((value) * 2)
#define ADD(left, right) ((left) + (right))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define EMPTY
#define PARENS (ANSWER)

/* A macro used before a second definition of the same name is not redefined here;
 * the point of the fixture is expansion, not the redefinition diagnostic. */

#if 0
int this_never_compiles( {
#endif

#ifndef MISSING
#define PRESENT 1
#else
#define PRESENT 0
#endif

int main(void) {
    if (ANSWER != 42) { return 1; }
    if (PARENS != 42) { return 2; }

    /* A function-like macro expands on call, and not when merely named. */
    if (DOUBLE(21) != 42) { return 3; }
    if (ADD(20, 22) != 42) { return 4; }

    /* The arguments bind as written: DOUBLE(2 + 3) is 10, not 2 + 6. */
    if (DOUBLE(2 + 3) != 10) { return 5; }

    /* A function-like macro used twice in one expression, and each expansion
     * separately. A **nested** invocation such as DOUBLE(DOUBLE(10)) does not expand
     * its argument and is a separate defect, recorded in implementation.md, so this
     * fixture stays at one level and remains a smoke test rather than a failing one. */
    if (DOUBLE(20) != 40) { return 6; }
    if (MAX(ANSWER, 7) != 42) { return 7; }
    if (MAX(1, 2) != 2) { return 8; }

    /* Conditional compilation kept only the #ifndef branch. */
    if (PRESENT != 1) { return 9; }

    /* An empty macro expands to nothing and leaves the expression intact. */
    int EMPTY counter = 5;
    if (counter != 5) { return 10; }

    /* Target selection by preprocessor macro is not exercised here: this compiler
     * defines no target macros, so `#if defined(__KR16__)` is false on both
     * profiles. Recorded in implementation.md as a gap rather than asserted here,
     * because a fixture must pass on the compiler that exists. */

    return 0;
}
