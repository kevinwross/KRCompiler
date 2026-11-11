class Base {
public:
    Base() { value = 5; }
    int value;
};

class Derived : public Base {
};

Derived global;

int main() {
    return global.value;
}
