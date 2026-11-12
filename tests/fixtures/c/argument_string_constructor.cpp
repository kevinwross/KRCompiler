struct S { const char *text; S(const char *input) : text(input) {} };
int take(S s) { return s.text[0] == 'a' ? 0 : 1; }
int main() { return take("ab"); }
