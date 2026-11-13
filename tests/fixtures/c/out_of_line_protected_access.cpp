// An out-of-line member body has class scope: an unqualified name resolves to a
// member of the defining class, and a derived class reaches a protected member
// of its base. main returns 3 when the read and the write both work.
class Base {
  protected:
    int shared;

  public:
    Base() : shared(1) {}
};

class Derived : public Base {
  public:
    int read();
    void write(int value);
};

int Derived::read() { return shared; }

void Derived::write(int value) { shared = value; }

int main() {
    Derived derived;
    derived.write(3);
    return derived.read();
}
