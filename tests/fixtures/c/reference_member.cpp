class Ref {
public:
    Ref(int& value) : value_(value) {}
    int get() { return value_; }
private:
    int& value_;
};
int main() {
    int value = 3;
    Ref ref(value);
    value = 4;
    return ref.get();
}
