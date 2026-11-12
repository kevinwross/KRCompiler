struct Outer {
    struct Inner {
        int value;

        Inner(int input) : value(input) {}
    };
};

int main() {
    Outer::Inner item(3);
    return item.value;
}
