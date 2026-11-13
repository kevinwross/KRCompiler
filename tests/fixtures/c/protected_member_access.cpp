// A protected member is not reachable from a free function either. The
// compiler must reject the access below.
class Base {
  protected:
    int shared;

  public:
    Base() : shared(1) {}
};

int main() {
    Base base;
    return base.shared;
}
