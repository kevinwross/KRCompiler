// A record returned from a function declared to return an integer needs a
// conversion operator. Without one the return value has no type, and the
// diagnostic names that instead of leaving invalid IR behind.

struct Plain {
    int value;
};

Plain make(void) {
    Plain result;
    result.value = 7;
    return result;
}

int take(void) { return make(); }

int main(void) { return take(); }
