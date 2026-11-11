class Inner {
public:
    Inner() { value = 5; }
    int value;
};

class Outer {
public:
    Inner inner;
};

int main() {
    Outer object;
    return object.inner.value;
}
