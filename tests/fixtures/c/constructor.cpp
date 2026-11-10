class Counter {
public:
    int value;
    Counter() { value = 7; }
    int get() { return value; }
};

int main() {
    Counter counter;
    return counter.get();
}
