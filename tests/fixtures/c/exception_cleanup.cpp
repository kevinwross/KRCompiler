// Destructor cleanup on propagation is not implemented yet, so a `try` with a
// live automatic object whose destructor must run is rejected rather than
// silently skipped (ADR 0026).
struct Payload {
    int value;
};

struct Guard {
    ~Guard() {}
};

int main() {
    Guard guard;
    try {
        Payload payload;
        payload.value = 1;
        throw payload;
    } catch (Payload& caught) {
        return caught.value;
    }
    return 0;
}
