int main(void) {
    int integer = 1;
    double real = 1.5;
    int *pointer = &integer;
    int first = _Generic(integer, int : 3, default : 4);
    int second = _Generic(real, int : 6, double : 7, default : 8);
    int third = _Generic(pointer, int * : 9, default : 10);
    return first == 3 && second == 7 && third == 9 ? 0 : 1;
}
