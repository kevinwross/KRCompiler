int main() {
    int values[3] = {1, 2, 3};
    int sum = 0;
    for (int value : values) {
        sum = sum + value;
    }
    return sum;
}
