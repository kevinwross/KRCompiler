// Reduced reproducer for the allocation defect recorded in implementation.md:
// Interleaving malloc(16) with strdup in one loop halts from 12 iterations on,
// while either allocation alone is fine. Linked but not executed until fixed.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Node {
    char *name;
    int height;
    struct Node *left;
    struct Node *right;
};

static const char *names[] = {
    "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf",
    "Hotel", "India", "Juliet", "Kilo", "Lima", "Mike", "November",
    "Oscar", "Papa", "Quebec", "Romeo", "Sierra", "Tango", "Uniform",
    "Victor"
};

int main(void) {
    struct Node *root = NULL;
    struct Node *tail = NULL;
    int index = 0;
    while (index < 22) {
        struct Node *node = (struct Node *)malloc(sizeof(struct Node));
        if (!node) { return 100; }
        node->name = strdup(names[index]);
        if (!node->name) { return 101; }
        node->height = index;
        node->left = NULL;
        node->right = NULL;
        if (!root) { root = node; } else { tail->right = node; }
        tail = node;
        index = index + 1;
    }
    int count = 0;
    struct Node *walk = root;
    while (walk) {
        if (!walk->name) { return 102; }
        if (walk->height != count) { return 103; }
        if (strcmp(walk->name, names[count]) != 0) { return 104; }
        count = count + 1;
        walk = walk->right;
    }
    printf("walked %d nodes\n", count);
    return count;
}
