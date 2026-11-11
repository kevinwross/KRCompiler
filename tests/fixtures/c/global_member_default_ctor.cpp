class Inner {
public:
    Inner() { value = 5; }
    int value;
};

class Outer {
public:
    Inner inner;
};

Outer global;

int main() {
    return global.inner.value;
}
