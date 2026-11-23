struct triple { char a; char b; char c; };
struct triple make(void) { struct triple t; t.a = 4; t.b = 5; t.c = 6; return t; }
int main(void) { struct triple v = make(); return v.a + v.b + v.c; }
