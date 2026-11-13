// An in-class member body is checked in its class's scope, so it may reach its
// own members but not another class's private member. The compiler must reject
// the access below.
class Secret {
    int value;

  public:
    Secret() : value(7) {}
};

class Reader {
  public:
    int peek(Secret const &secret) { return secret.value; }
};

int main() {
    Reader reader;
    Secret secret;
    return reader.peek(secret);
}
