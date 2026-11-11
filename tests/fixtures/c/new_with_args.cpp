class Counter { public: Counter(int v) { value = v; } int get() { return value; } private: int value; };
int main() { Counter *p = new Counter(5); int v = p->get(); delete p; return v; }
