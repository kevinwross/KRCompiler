// Nested function-like macro invocation.
//
// A macro's arguments are expanded **before** substitution, and the macro being
// expanded is not yet disabled at that point, so `DOUBLE(DOUBLE(10))` expands the inner
// invocation first. This compiler passed the wrong disabled set, so the inner `DOUBLE`
// was substituted unexpanded and the rescan could not recover it: the preprocessed text
// was `( ( D ( 10 ) ) * 2 )` and the program failed to link with an implicit call.
//
// Stringize and paste must keep the **raw** argument, so they are checked here too --
// they are the part of substitution that must not pre-expand.
//
// Returns 42.

#define DOUBLE(v) ((v) * 2)
#define ADD(a, b) ((a) + (b))
#define STRINGIFY(x) #x
#define PASTE(a, b) a##b

int my_value = 42;

int main(void) {
    // One level of nesting, and two.
    if (DOUBLE(DOUBLE(10)) != 40) { return 1; }
    if (DOUBLE(DOUBLE(DOUBLE(1))) != 8) { return 2; }

    // The inner macro may be a different one.
    if (DOUBLE(ADD(1, 2)) != 6) { return 3; }
    if (ADD(DOUBLE(1), DOUBLE(2)) != 6) { return 4; }

    // An argument that is itself a macro call, several times over:
    // DOUBLE(1) is 2, ADD(2, 1) is 3, DOUBLE(3) is 6, DOUBLE(6) is 12.
    if (DOUBLE(DOUBLE(ADD(DOUBLE(1), 1))) != 12) { return 5; }

    // Stringize takes the argument unexpanded: the text starts with the macro name.
    char* text = STRINGIFY(DOUBLE(1));
    if (text[0] != 'D') { return 6; }
    if (text[1] != 'O') { return 7; }

    // Paste joins the raw tokens into an identifier that already exists.
    if (PASTE(my_, value) != 42) { return 8; }

    return 42;
}
