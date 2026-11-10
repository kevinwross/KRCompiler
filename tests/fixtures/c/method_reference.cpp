class Counter {
public:
    void increment(int& value) { value = value + 1; }
};

int main() {
    int number = 3;
    Counter counter;
    counter.increment(number);
    return number;
}
