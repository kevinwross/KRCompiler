class Base {
public:
    int operator[](int index) { return 3 + index; }
};
class Derived : public Base {
};
int main() {
    Derived value;
    return value[2];
}
