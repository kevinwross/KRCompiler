int main(void) {
    int value = 3;
    int *left = &value, *right = &value;
    return *left + *right - 6;
}
