int classify(int value) {
    if (value == 1) {
        return 3;
    }
    if (value == 2) {
        return 5;
    }
    return 7;
}

int main(void) {
    int total = 0;
    for (int index = 0; index < 3; index++) {
        total += classify(index);
    }
    return total;
}
