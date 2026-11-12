int destroyed = 0;
struct S { ~S(); };
S::~S() { destroyed++; }
int main() { { S s; } return destroyed; }
