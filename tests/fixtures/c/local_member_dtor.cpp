int puts(const char *text);

class Inner {
public:
    Inner() { value = 5; }
    ~Inner() { puts("inner"); }
    int value;
};

class Outer {
public:
    Inner inner;
};

int main() {
    Outer object;
    puts("main");
    return object.inner.value;
}
