struct Node {
    int value;
    struct Node *next;
};

int main(void) {
    struct Node node;
    node.value = 5;
    node.next = 0;
    return node.value;
}
