// Comparing two records without an operator is a program error, not a compiler
// bug: a record has no scalar form to compare. Before this the comparison was
// emitted as an aggregate icmp and the IR verifier reported it instead.

struct Item {
    int value;
};

int main() {
    Item first;
    Item second;
    if (first == second) {
        return 1;
    }
    return 42;
}