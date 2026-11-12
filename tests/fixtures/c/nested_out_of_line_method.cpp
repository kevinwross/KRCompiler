struct Outer { struct Inner { int get(); }; };
int Outer::Inner::get() { return 3; }
int main() { Outer::Inner item; return item.get(); }
