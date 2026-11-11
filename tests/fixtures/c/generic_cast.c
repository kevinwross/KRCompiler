int main(void) {
    int integer = 1;
    int chosen = _Generic((double)integer, int : 4, double : 11, default : 12);
    return chosen == 11 ? 0 : 1;
}
