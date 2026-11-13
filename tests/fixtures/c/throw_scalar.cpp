// Exceptions in the supported subset carry a class object and its typeinfo; a
// scalar has neither, so it is rejected rather than thrown as a raw value.
int main() {
    try {
        throw 5;
    } catch (...) {
        return 1;
    }
    return 0;
}
