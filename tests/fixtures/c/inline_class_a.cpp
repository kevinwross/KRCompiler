class Counter {
public:
    int add(int left, int right) { return left + right; }
};

int helper() {
    Counter counter;
    return counter.add(1, 2);
}
