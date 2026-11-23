struct triple { char a; char b; char c; };
int sum3(struct triple t) { return t.a + t.b + t.c; }
int main(void) { struct triple v; v.a = 1; v.b = 2; v.c = 3; return sum3(v); }
