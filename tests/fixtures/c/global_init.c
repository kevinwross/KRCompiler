int square(int value) {
    return value * value;
}

int global = square(3);

int main(void) {
    return global;
}
