// A reference dynamic_cast needs std::bad_cast, which needs exceptions; the
// compiler must say so rather than lower a cast it cannot fail safely.
struct Base {
    virtual int value() const { return 1; }
};

struct Derived : Base {
    int value() const override { return 2; }
};

int main() {
    Derived object;
    Base& base = object;
    Derived& down = dynamic_cast<Derived&>(base);
    return down.value();
}
