int main() {
    int* values = new int[4];
    values[0] = 7;
    int result = values[0];
    delete[] values;
    return result;
}
