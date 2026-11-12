class S {
    int value;

  public:
    S() : value(7) {}
    friend int get(S const &);
};

int get(S const &s) { return s.value; }

int main() {
    S s;
    return get(s);
}
