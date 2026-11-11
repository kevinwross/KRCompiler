struct Bits {
    unsigned int low : 3;
    unsigned int high : 5;
};

int main(void) {
    struct Bits bits = {.low = 5, .high = 17};
    return bits.low + bits.high;
}
