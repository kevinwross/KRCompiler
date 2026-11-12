struct Outer { struct Inner { int value; }; };
int main() { Outer::Inner item; item.value = 3; return item.value; }
