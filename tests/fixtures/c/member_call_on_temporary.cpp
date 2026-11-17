// A member call on a temporary is rejected rather than crashing the compiler.
//
// `Pack<int>().count()` reaches code generation with no object to call on: the class
// has no user-declared constructor, so the temporary has nothing to initialise, and
// the member access then has no record value. Before the conditional-branch recovery
// was added, the resulting null operand reached the back end and the compiler faulted
// with an access violation instead of reporting the error.
//
// This file is compiled by `krc_check_diag_member_call_on_temporary`, which requires
// the diagnostic below and a non-zero exit. The named-object form is in
// `tests/corpus/extracted/tier2/templates.cpp`.

template <typename... Ts>
class Pack {
public:
    int count() const { return sizeof...(Ts); }
};

int main() {
    int value = Pack<int>().count();
    return value;
}
