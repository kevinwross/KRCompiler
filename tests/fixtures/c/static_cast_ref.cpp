int main() { int value = 3; int &reference = static_cast<int &>(value); reference = 7; return value; }
