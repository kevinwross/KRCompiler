int main() {
    int base = 3;
    auto add = [value = base + 1](int amount) { return value + amount; };
    base = 10;
    return add(3);
}
