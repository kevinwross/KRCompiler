class Node {
public:
    int value;
    Node() { value = 9; }
};

int main() {
    Node* node = new Node;
    int value = node->value;
    delete node;
    return value;
}
