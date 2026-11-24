/* A process-table search whose result must be the pid it stored.
 *
 * The kr32os kernel walks a 256-entry table of 48-byte records to find a free
 * slot, writes into it, and reads the field back. Compiled at -O1 this returned
 * a value read from the wrong address: the address of `table[index]` was
 * computed as `table + table * 48`, because the register allocated to the result
 * of the index expression was also the register the address computation copied
 * the base into. The allocator now makes a definition interfere with its own
 * operands, which is what this fixture pins.
 *
 * Exit code 0 means every check held.
 */

#define ENTRIES 256

struct Record {
    int used;
    int pid;
    int state;
    int status;
    char name[32];
};

static struct Record table[ENTRIES];
static int next_pid = 1;

static int spawn(const char *name) {
    int index = 0;
    while (index < ENTRIES && table[index].used != 0) {
        ++index;
    }
    if (index >= ENTRIES) {
        return -1;
    }
    table[index].used = 1;
    table[index].pid = next_pid++;
    table[index].state = 1;
    table[index].status = 0;
    int offset = 0;
    while (name[offset] != '\0' && offset < 31) {
        table[index].name[offset] = name[offset];
        ++offset;
    }
    table[index].name[offset] = '\0';
    return table[index].pid;
}

int find(const char *name) {
    int index = 0;
    while (index < ENTRIES && table[index].used != 0) {
        if (table[index].name[0] == name[0] && table[index].name[1] == name[1]) {
            return table[index].pid;
        }
        ++index;
    }
    return -1;
}

int main(void) {
    int first = spawn("init");
    int second = spawn("sh");
    if (first != 1) {
        return 1;
    }
    if (second != 2) {
        return 2;
    }
    if (table[0].used != 1 || table[0].pid != 1) {
        return 3;
    }
    if (table[1].pid != 2 || table[1].name[1] != 'h') {
        return 4;
    }
    if (find("init") != 1) {
        return 5;
    }
    if (find("sh") != 2) {
        return 6;
    }
    return 0;
}
