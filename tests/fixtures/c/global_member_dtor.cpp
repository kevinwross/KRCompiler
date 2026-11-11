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

Outer global;

int main() {
    puts("main");
    return global.inner.value;
}
