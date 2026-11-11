int main(void) {
    int values[4] = {1, 2, 3, 4};
    int *begin = values;
    int *end = values + 3;
    return end - begin;
}
