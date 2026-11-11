int puts(const char *text);

class Inner {
public:
    ~Inner() { puts("inner"); }
};

class Outer {
public:
    Inner inner;
};

int main() {
    Outer *object = new Outer();
    puts("main");
    delete object;
    return 0;
}
