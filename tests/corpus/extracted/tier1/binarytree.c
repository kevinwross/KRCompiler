/* Extracted from ../kr32/examples at commit ba69940 for the Tier 0-1 corpus.
 * Copied verbatim; the original header, if any, follows. */
// Self-balancing AVL tree of character strings.
//
// Each insertion rebalances the tree with single or double rotations, so the
// height stays O(log n). The driver inserts 30 famous engineers and prints the
// tree horizontally: the root starts at column zero, and each level adds five
// spaces. Right (greater) subtrees print above the node, left subtrees below.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Node {
    char *name;
    int height;
    struct Node *left;
    struct Node *right;
};

int node_height(struct Node *node) {
    if (node == 0) {
        return 0;
    }
    return node->height;
}

int max_int(int left, int right) {
    if (left > right) {
        return left;
    }
    return right;
}

void update_height(struct Node *node) {
    node->height = 1 + max_int(node_height(node->left), node_height(node->right));
}

struct Node *new_node(const char *name) {
    struct Node *node = (struct Node *)malloc(sizeof(struct Node));
    if (node == 0) {
        return 0;
    }
    node->name = strdup(name);
    if (node->name == 0) {
        free(node);
        return 0;
    }
    node->height = 1;
    node->left = 0;
    node->right = 0;
    return node;
}

struct Node *rotate_right(struct Node *root) {
    struct Node *left = root->left;
    struct Node *middle = left->right;
    left->right = root;
    root->left = middle;
    update_height(root);
    update_height(left);
    return left;
}

struct Node *rotate_left(struct Node *root) {
    struct Node *right = root->right;
    struct Node *middle = right->left;
    right->left = root;
    root->right = middle;
    update_height(root);
    update_height(right);
    return right;
}

struct Node *balance_node(struct Node *node) {
    update_height(node);
    int balance = node_height(node->left) - node_height(node->right);
    if (balance > 1) {
        if (node_height(node->left->left) < node_height(node->left->right)) {
            node->left = rotate_left(node->left);
        }
        return rotate_right(node);
    }
    if (balance < -1) {
        if (node_height(node->right->right) < node_height(node->right->left)) {
            node->right = rotate_right(node->right);
        }
        return rotate_left(node);
    }
    return node;
}

struct Node *insert(struct Node *root, const char *name) {
    if (root == 0) {
        return new_node(name);
    }
    int comparison = strcmp(name, root->name);
    if (comparison < 0) {
        root->left = insert(root->left, name);
    } else if (comparison > 0) {
        root->right = insert(root->right, name);
    } else {
        return root;
    }
    return balance_node(root);
}

int check_balanced(struct Node *node) {
    if (node == 0) {
        return 0;
    }
    int left_height = check_balanced(node->left);
    if (left_height < 0) {
        return -1;
    }
    int right_height = check_balanced(node->right);
    if (right_height < 0) {
        return -1;
    }
    int difference = left_height - right_height;
    if (difference > 1 || difference < -1) {
        return -1;
    }
    return 1 + max_int(left_height, right_height);
}

void print_tree(struct Node *node, int indent) {
    if (node == 0) {
        return;
    }
    print_tree(node->right, indent + 5);
    int spaces = 0;
    while (spaces < indent) {
        putchar(' ');
        spaces = spaces + 1;
    }
    printf("%s\n", node->name);
    print_tree(node->left, indent + 5);
}

void free_tree(struct Node *node) {
    if (node == 0) {
        return;
    }
    free_tree(node->left);
    free_tree(node->right);
    free(node->name);
    free(node);
}

int main(void) {
    struct Node *root = 0;

    root = insert(root, "Archimedes");
    root = insert(root, "Hero");
    root = insert(root, "Brunelleschi");
    root = insert(root, "Vinci");
    root = insert(root, "Watt");
    root = insert(root, "Trevithick");
    root = insert(root, "Stephenson");
    root = insert(root, "Brunel");
    root = insert(root, "Eiffel");
    root = insert(root, "Roebling");
    root = insert(root, "Tesla");
    root = insert(root, "Edison");
    root = insert(root, "Marconi");
    root = insert(root, "Wright");
    root = insert(root, "Ford");
    root = insert(root, "Diesel");
    root = insert(root, "Benz");
    root = insert(root, "Otto");
    root = insert(root, "Parsons");
    root = insert(root, "Whittle");
    root = insert(root, "Braun");
    root = insert(root, "Korolev");
    root = insert(root, "Shannon");
    root = insert(root, "Turing");
    root = insert(root, "Hopper");
    root = insert(root, "Kilby");
    root = insert(root, "Noyce");
    root = insert(root, "Moore");
    root = insert(root, "Cerf");
    root = insert(root, "Kahn");

    int height = check_balanced(root);
    if (height < 0) {
        printf("tree is not balanced\n");
        free_tree(root);
        return 1;
    }

    printf("AVL tree of engineers (height %d, 30 names)\n\n", height);
    print_tree(root, 0);
    free_tree(root);
    return 0;
}
