class Base {
public:
    int operator()(int value) { return 3 + value; }
};
class Derived : public Base {
};
int main() {
    Derived value;
    return value(2);
}
