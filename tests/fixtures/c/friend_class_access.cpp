// `friend class Other;` confers access and declares no member, so S still holds
// one int rather than a copy of Other's fields. Other's out-of-line member body
// may then read S's private member.
class S {
    friend class Other;
    int value;

  public:
    S() : value(7) {}
};

class Other {
  public:
    int first;
    int second;
    int peek(S const &secret);
};

int Other::peek(S const &secret) { return secret.value; }

static_assert(sizeof(S) == 4, "a friend class declaration must not add members");

int main() {
    S secret;
    Other other;
    other.first = 1;
    other.second = 2;
    return other.peek(secret);
}
