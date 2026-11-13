struct Base {
    virtual int value() const final { return 1; }
};
struct Derived : Base {
    int value() const override { return 2; }
};
int main() { Derived object; return object.value(); }
