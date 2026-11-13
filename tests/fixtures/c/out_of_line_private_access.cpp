// A member function body may reach its own class's members unqualified, but not
// another class's private member. The compiler must reject the access below.
class Secret {
    int value;

  public:
    Secret() : value(7) {}
};

class Reader {
  public:
    int peek(Secret const &secret);
};

int Reader::peek(Secret const &secret) { return secret.value; }

int main() {
    Reader reader;
    Secret secret;
    return reader.peek(secret);
}
