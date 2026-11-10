class Counter {
public:
    Counter(int value) : value_(value) {}
    int get() { return value_; }
private:
    int value_;
};
Counter global(5);
int main() {
    return global.get();
}
