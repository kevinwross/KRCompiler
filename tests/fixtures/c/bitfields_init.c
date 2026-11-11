struct Bits {
    unsigned int low : 3;
    unsigned int high : 5;
};

int main(void) {
    struct Bits bits = {5, 17};
    return bits.low + bits.high;
}
