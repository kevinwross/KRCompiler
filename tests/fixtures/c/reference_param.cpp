void increment(int& value) {
    value = value + 1;
}

int main() {
    int number = 3;
    increment(number);
    return number;
}
