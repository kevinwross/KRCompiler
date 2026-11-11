struct Bits {
    unsigned int low : 3;
    unsigned int high : 5;
};

struct Holder {
    struct Bits bits;
};

int main(void) {
    struct Holder holder = {.bits.low = 5, .bits.high = 17};
    return holder.bits.low + holder.bits.high;
}
