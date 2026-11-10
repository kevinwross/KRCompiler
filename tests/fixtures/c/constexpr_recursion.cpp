constexpr int factorial(int value) {
    return value <= 1 ? 1 : value * factorial(value - 1);
}

constexpr int count = factorial(3);

int main() {
    int values[count];
    values[5] = 7;
    return values[5];
}
