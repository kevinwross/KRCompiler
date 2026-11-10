int seed(int value) {
    static int stored = value;
    return stored;
}

int main(void) {
    return seed(3) + seed(5);
}
