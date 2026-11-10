constexpr int triple(int value) { return value * 3; }

int main() {
    constexpr int base = 4;
    return triple(base);
}
