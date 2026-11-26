int main(void) {
    int sum = 0;
    for (int i = 0; i < 200000; i++) {
        sum += (i ^ (i >> 3)) & 255;
    }
    return sum & 127;
}
