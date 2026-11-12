int main() { int value = 7; int *pointer = &value; void *raw = reinterpret_cast<void *>(pointer); int *back = reinterpret_cast<int *>(raw); return *back; }
