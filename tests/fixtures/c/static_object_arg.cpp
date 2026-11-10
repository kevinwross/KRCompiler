class Counter {
public:
    Counter(int value) : value_(value) {}
    int get() { return value_; }
private:
    int value_;
};
int read_counter() {
    static Counter counter(5);
    return counter.get();
}
int main() {
    return read_counter();
}
