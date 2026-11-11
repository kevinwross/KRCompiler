struct Pair {
    int left;
    int right;
};

struct Box {
    struct Pair pair;
    int tag;
};

struct Pair global_pair = {10, 20};

int main(void) {
    struct Pair local = {5, 7};
    struct Pair copy = local;
    struct Pair values[2];
    struct Box box;
    values[0].left = 1;
    values[0].right = 2;
    values[1] = copy;
    box.pair = values[1];
    box.tag = global_pair.left + global_pair.right;
    return local.left + local.right + copy.left + copy.right +
           values[0].left + values[0].right + values[1].left +
           values[1].right + box.pair.left + box.pair.right + box.tag;
}
