int main(void) {
    int value = 5;
    return ((value << 2) | (value >> 1)) + (~value & 15);
}
