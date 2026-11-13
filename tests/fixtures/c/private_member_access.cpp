// A `class` member is private until an access specifier says otherwise, so a
// free function may not read it. The compiler must reject the access below.
class S {
    int value;

  public:
    S() : value(7) {}
};

int main() {
    S s;
    return s.value;
}
