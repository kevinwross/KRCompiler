// A call has no address of its own; assigning its result to a struct variable
// must copy through a temporary. Returns 2 when the second assignment lands.
struct Value {
    int first;
    int second;
};

Value make(int seed) {
    Value result;
    result.first = seed;
    result.second = seed + 1;
    return result;
}

int main() {
    Value value = make(1);
    value = make(value.first + 1);
    return value.first;
}
