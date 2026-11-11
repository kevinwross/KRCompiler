volatile int counter = 1;

int main(void) {
    volatile int local = 2;
    counter = counter + local;
    local = counter;
    return local == 3 ? 0 : 1;
}
