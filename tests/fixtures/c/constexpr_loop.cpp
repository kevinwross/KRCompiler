constexpr int sumTo(int count) {
    int total = 0;
    for (int index = 1; index <= count; index++) {
        total = total + index;
    }
    return total;
}

constexpr int count = sumTo(3);

int main() {
    int values[count];
    values[5] = 7;
    return values[5];
}
