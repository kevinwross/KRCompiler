struct S { int value; };
int main() { S items[2]; items[0].value = 3; items[1].value = 4; int sum = 0; for (S const &item : items) { sum += item.value; } return sum; }
