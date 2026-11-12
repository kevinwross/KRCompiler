int main() { int *values = new int[]{1, 2, 3}; int sum = values[0] + values[1] + values[2]; delete[] values; return sum; }
