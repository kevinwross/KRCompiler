#include <tuple>

// A pack whose element is a pointer. This is the shape item 31 found deducing `int*` as
// `int`, and it is **still** rejected after the per-element parameter typing landed: the
// error is `IR verifier: function 'main': store type does not match its pointer`, on both
// targets. So it is a separate defect from the width bug, not a symptom of it.
//
// The fixture exists to be rejected, so that the day it starts compiling this test fails
// and whoever fixes it is told to turn it into a passing test. Nothing here is a claim
// that pointer elements should not work -- they should, and `tuple<int*, int>` built in
// place does work; it is `make_tuple` with a pointer argument that does not.
int main() {
    int value = 41;
    int* p = &value;
    std::tuple<int*, int> t = std::make_tuple(p, 1);
    if (std::tuple_size(t) != 2) { return 1; }
    if (std::tuple_head(std::tuple_tail(t)) != 1) { return 2; }

    int* head = std::tuple_head(t);
    if (head == nullptr) { return 3; }
    if (*head != 41) { return 4; }
    if (p != head) { return 5; }
    return 42;
}
