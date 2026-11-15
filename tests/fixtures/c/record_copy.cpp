// A plain copy of a record copies every field. The initialiser used to emit a
// whole-record load and store, which the backends move as one word, so a
// two-field copy kept its first field and zeroed the rest. The copy now comes
// from the source's address, field by field. Returns 42.

struct Pair {
    int first;
    int second;
};

struct Triple {
    int first;
    int second;
    int third;
};

int main() {
    Pair pair;
    pair.first = 7;
    pair.second = 9;
    Pair pair_copy(pair);
    if (pair_copy.first != 7 || pair_copy.second != 9) {
        return 1;
    }

    // The source is unchanged and the two objects are independent.
    pair_copy.first = 1;
    if (pair.first != 7) {
        return 2;
    }

    Triple triple;
    triple.first = 1;
    triple.second = 2;
    triple.third = 3;
    Triple triple_copy(triple);
    if (triple_copy.first != 1 || triple_copy.second != 2 ||
        triple_copy.third != 3) {
        return 3;
    }

    // An assignment copies every field too.
    Triple assigned;
    assigned.first = 0;
    assigned.second = 0;
    assigned.third = 0;
    assigned = triple;
    if (assigned.first != 1 || assigned.second != 2 || assigned.third != 3) {
        return 4;
    }
    return 42;
}