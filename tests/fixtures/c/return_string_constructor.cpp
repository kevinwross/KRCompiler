struct S { const char *text; S(const char *input) : text(input) {} };
S make() { return "ab"; }
int main() { S s = make(); return s.text[0] == 'a' ? 0 : 1; }
