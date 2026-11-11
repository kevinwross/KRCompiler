struct Pair {
    int value;
};

volatile int counter = 1;
volatile struct Pair pair = {4};

int main(void) {
    volatile int local = 2;
    volatile struct Pair *pointer = &pair;
    counter = counter + local;
    local = counter;
    pair.value = pair.value + 1;
    pointer->value = pointer->value + 1;
    return local == 3 && pair.value == 6 ? 0 : 1;
}
