int constructions = 0;

struct Member {
    Member() {
        constructions++;
    }

    ~Member() {}
};

struct Base {
    Base() {
        constructions += 10;
    }

    virtual ~Base() {}
};

struct Derived : Base {
    Member member;

    ~Derived() {}
};

int main() {
    Derived *object = new Derived();
    delete object;
    return constructions == 11 ? 0 : 1;
}
