struct triple { char a; char b; char c; };
int sum3(struct triple t) { return t.a + t.b + t.c; }
struct triple make(void) { struct triple t; t.a = 4; t.b = 5; t.c = 6; return t; }
int main(void) { return sum3(make()); }
