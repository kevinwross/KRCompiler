#include <stdio.h>

class D {
public:
    ~D() { printf("destructor\n"); }
};

int main() {
    D object;
    return 0;
}
