class Counter {
public:
    int add(int left, int right) { return left + right; }
};

int helper();

int main() {
    Counter counter;
    return helper() + counter.add(3, 0);
}
