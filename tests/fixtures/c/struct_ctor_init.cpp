struct Value {
    int value;

    Value(int input) : value(input) {}
};

struct Box : Value {
    Box(int input) : Value(input) {}
};

int main() {
    Box box(7);
    return box.value;
}
