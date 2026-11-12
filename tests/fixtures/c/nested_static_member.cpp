struct Outer { struct Inner { static int value; }; };
int Outer::Inner::value = 3;
int main() { return Outer::Inner::value; }
