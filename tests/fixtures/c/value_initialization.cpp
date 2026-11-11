struct Value {
    int value;

    Value() : value(3) {}
    Value(int input) : value(input) {}
};

int main() {
    Value first = Value();
    Value second = Value(7);
    return first.value + second.value;
}
