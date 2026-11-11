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
    Outer *object = new Outer();
    int value = object->inner.value;
    delete object;
    return value;
}
