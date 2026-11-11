int main() {
    int integer = 3;
    double real = 1.5;
    decltype(integer + 1) sum = integer + 4;
    decltype(real * 2.0) scaled = real * 2.0;
    return sum == 7 && scaled == 3.0 ? 0 : 1;
}
