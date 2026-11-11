int main(void) {
    int integer = 1;
    double real = 1.5;
    int first = _Generic(integer, int : 3, default : 4);
    int second = _Generic(real, int : 6, double : 7, default : 8);
    return first == 3 && second == 7 ? 0 : 1;
}
