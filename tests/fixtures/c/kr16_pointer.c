int value = 5;

int main(void) {
    int *pointer = &value;
    *pointer = *pointer + 2;
    return value;
}
