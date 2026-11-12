namespace ns { struct S { int value; }; }
int main() { ns::S object; object.value = 7; ns::S *pointer = new ns::S; pointer->value = 3; int result = object.value + pointer->value; delete pointer; return result; }
