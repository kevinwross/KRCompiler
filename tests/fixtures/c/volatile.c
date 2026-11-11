struct Pair {
    int value;
};

volatile int counter = 1;
volatile struct Pair pair = {4};

int main(void) {
    volatile int local = 2;
    counter = counter + local;
    local = counter;
    pair.value = pair.value + 1;
    return local == 3 && pair.value == 5 ? 0 : 1;
}
