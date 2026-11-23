class Animal {
public:
    virtual int legs() const { return 4; }
    virtual ~Animal() {}
};

class Bird : public Animal {
public:
    int legs() const override { return 2; }
};

int main() {
    Bird bird;
    unsigned vptr = *(unsigned*)&bird;
    unsigned reserved = *(unsigned*)(vptr - 4);
    return (int)reserved;
}
