// Destruction of several objects in one scope (ADR 0027): each construction
// that can unwind protects exactly the objects built before it, so a scope may
// hold more than one destructible object and the destructors still run in
// reverse declaration order. Returns 42 when every count and order holds.

int destructions = 0;
int order[8];
int order_count = 0;

struct Tracked {
    int id;
    Tracked(int id_in) : id(id_in) {}
    ~Tracked() {
        if (order_count < 8) {
            order[order_count] = id;
            order_count = order_count + 1;
        }
        destructions = destructions + 1;
    }
};

int main() {
    {
        // Three objects in one scope: two share a type.
        Tracked first(1);
        Tracked second(2);
        Tracked third(3);
        if (first.id != 1 || second.id != 2 || third.id != 3) {
            return 1;
        }
    }
    if (destructions != 3) {
        return 2;
    }
    // Reverse declaration order.
    if (order_count != 3 || order[0] != 3 || order[1] != 2 || order[2] != 1) {
        return 3;
    }

    {
        // Nested scopes destroy inner objects before outer ones.
        Tracked outer(4);
        {
            Tracked inner(5);
        }
        if (destructions != 4) {
            return 4;
        }
    }
    if (destructions != 5) {
        return 5;
    }
    if (order[3] != 5 || order[4] != 4) {
        return 6;
    }

    // An early return still destroys what is live.
    for (int index = 0; index < 1; index = index + 1) {
        Tracked loop_object(6);
        Tracked loop_object_two(7);
        if (index == 0) {
            continue;
        }
        return 7;
    }
    if (destructions != 7) {
        return 8;
    }
    if (order[5] != 7 || order[6] != 6) {
        return 9;
    }
    return 42;
}
