// A three-byte record passed by value. docs/abi.md says an aggregate of eight
// bytes or less stays in integer registers, so on KR32 the callee receives it in
// registers; KR16 has no register path for aggregates and passes a hidden pointer
// instead, which its `aggregate_rules` says rather than a size threshold guessing
// it. The same source must work on both, with the caller's copy untouched by the
// callee's writes. Returns 42.
//
// The caller's copy is built from the argument's **address**, one store per field.
// Copying the loaded record instead moves a single machine word -- two bytes on
// KR16 -- which dropped this record's third byte and made the program return 1
// there, while KR32's four-byte word hid it.

struct Three { char a; char b; char c; };

int sum(Three value) {
    value.a = 0;
    return value.a + value.b + value.c;
}

int main() {
    Three three;
    three.a = 1;
    three.b = 2;
    three.c = 3;
    if (sum(three) != 5) { return 1; }
    // A memory-class argument is a copy, so the caller keeps its own.
    if (three.a != 1) { return 2; }
    return 42;
}
