// A three-byte record passed by value. docs/abi.md says an aggregate of eight
// bytes or less stays in integer registers, so on KR32 the callee receives it in
// registers; KR16 has no register path for aggregates and passes a hidden pointer
// instead, which its `aggregate_rules` says rather than a size threshold guessing
// it. The same source must work on both, with the caller's copy untouched by the
// callee's writes. Returns 42.
//
// KR16 is registered for the link only, and the reason is a separate defect: the
// caller's copy of a memory-class argument is emitted as one whole-record load and
// store, which the back end moves as one word -- two bytes on KR16, so the third
// byte never arrives and this program returns 1 there. KR32's four-byte word hides
// it. The copy wants the argument's address, which `create_call_instruction` does
// not have; that is recorded in implementation.md, not worked around here.

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
