struct S { int value; S() : value(7) {} };
int main() { S items[2]; return items[1].value; }
