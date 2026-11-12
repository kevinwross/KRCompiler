int main() { int *values = new int[3]{1}; int result = values[0] + values[1] + values[2]; delete[] values; return result; }
