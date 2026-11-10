struct Bits {
    unsigned int a : 3;
    unsigned int b : 5;
    int c : 4;
};

int main(void) {
    struct Bits bits;
    bits.a = 5;
    bits.b = 17;
    bits.c = -3;
    return (int)(bits.a + bits.b + bits.c);
}
