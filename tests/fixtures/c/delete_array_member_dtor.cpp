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
    Outer *objects = new Outer[2];
    puts("main");
    delete[] objects;
    return 0;
}
