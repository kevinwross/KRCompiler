// main() references nothing in gc_unused_object.cpp, so with --gc-sections the
// linker drops that object entirely: the image is the startup object plus this
// one function. Returns 42.

int main() { return 42; }