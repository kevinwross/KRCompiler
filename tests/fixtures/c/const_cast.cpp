int main() { int value = 3; int const &reference = value; int &mutable_reference = const_cast<int &>(reference); mutable_reference = 7; return value; }
