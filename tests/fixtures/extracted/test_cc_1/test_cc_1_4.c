int value = 3;
int *left = &value, *right = &value;
int main(void) { return *left + *right - 6; }
