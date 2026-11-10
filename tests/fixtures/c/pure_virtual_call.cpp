class Base {
public:
    virtual int value() = 0;
};
class Derived : public Base {
public:
    int value() override { return 7; }
};
int main() {
    Derived derived;
    Base* base = &derived;
    return base->value();
}
