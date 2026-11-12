int value() { thread_local int stored = 7; return stored; }
int main() { return value(); }
