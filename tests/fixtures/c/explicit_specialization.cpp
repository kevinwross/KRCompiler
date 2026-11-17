// An explicit specialization is selected for the type it specialises.
//
// `template <> class Named<int>` was registered but never instantiated: three separate
// guards each rejected it before it could build. Registration required a parameter list
// it does not have; the arity check compared the argument count against that empty list;
// and the binding loop walked the arguments while indexing the empty parameter list,
// which crashed the compiler outright. All three now treat a specialization as the
// distinct type it is.
//
// Returns 42.

template <typename T>
class Named {
public:
    int kind() const { return 0; }

    int width() const { return 4; }
};

template <>
class Named<int> {
public:
    int kind() const { return 1; }

    int width() const { return 8; }
};

template <>
class Named<char> {
public:
    int kind() const { return 2; }
};

int main() {
    // The specialised type answers from its own body.
    Named<int> special;
    if (special.kind() != 1) { return 1; }
    if (special.width() != 8) { return 2; }

    // A second specialization of the same template is independent of the first.
    Named<char> narrow;
    if (narrow.kind() != 2) { return 3; }

    // A type with no specialization still gets the primary.
    Named<long> general;
    if (general.kind() != 0) { return 4; }
    if (general.width() != 4) { return 5; }

    return 42;
}
