/* The KR16 profile has sixteen-bit int and long, so this constant has no type
   that can hold it and must be diagnosed rather than narrowed. */
long value(void) {
    return 5000000001L;
}

int main(void) {
    return (int)value();
}
