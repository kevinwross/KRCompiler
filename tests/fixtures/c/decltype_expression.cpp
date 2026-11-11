int main() {
    int integer = 3;
    decltype(integer + 1) sum = integer + 4;
    return sum == 7 ? 0 : 1;
}
