struct S { const char *text; S(const char *input) : text(input) {} };
int main() { S s = "ab"; return s.text[0] == 'a' ? 0 : 1; }
