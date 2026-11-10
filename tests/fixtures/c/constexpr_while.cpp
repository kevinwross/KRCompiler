constexpr int sumWhile(int count) {
    int total = 0;
    int index = 1;
    while (index <= count) {
        total = total + index;
        index++;
    }
    return total;
}

constexpr int count = sumWhile(3);

int main() {
    int values[count];
    values[5] = 7;
    return values[5];
}
