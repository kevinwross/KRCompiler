int puts(const char *text);

class Static {
public:
    Static() { puts("ctor"); }
    ~Static() { puts("dtor"); }
};

int read_static() {
    static Static value;
    return 0;
}

int main(void) {
    read_static();
    puts("main");
    return 0;
}
