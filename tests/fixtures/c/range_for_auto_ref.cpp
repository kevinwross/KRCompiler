int main() { int values[2] = {3, 4}; for (auto &value : values) { value += 1; } return values[0] + values[1]; }
