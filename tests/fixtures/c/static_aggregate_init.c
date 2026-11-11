struct Pair {
    int left;
    int right;
};

int total(void) {
    static int values[] = { 1, 2, 3 };
    static struct Pair pair = { 4, 5 };
    return values[0] + values[1] + values[2] + pair.left + pair.right;
}

int main(void) {
    return total();
}
