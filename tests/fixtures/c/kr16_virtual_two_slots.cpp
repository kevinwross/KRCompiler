// Two virtual functions, so a call reaches the second vtable slot.
//
// The stride was once the literal 4 while a KR16 pointer is two bytes, so slot 0 read offset 0 and
// worked, and slot 1 read offset 4 -- the second entry -- calling whatever sat there. The program
// raised an unhandled exception rather than returning a wrong value, which is why the exit status
// alone would have been enough to see it.
//
// Returns 42 on success, matching the other executed KR16 tests.

struct Base {
    int tag;
    virtual int first() { return 1; }
    virtual int second() { return 2; }
};

struct Derived : Base {
    virtual int first() { return 10; }
    virtual int second() { return 20; }
};

int main() {
    Derived derived;
    Base* pointer = &derived;

    if (pointer->first() != 10) {
        return 1;
    }
    if (pointer->second() != 20) {
        return 2;
    }
    return 42;
}
