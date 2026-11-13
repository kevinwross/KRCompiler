// C11 thread storage duration. The VM has no threads, so a thread-local
// object is a static object with a guarded initializer: it is created on first
// use, once, and lives for the rest of the run. Returns 42 when the file-scope
// object persists and the block-scope one initializes lazily and only once.

_Thread_local int file_scope = 0;

static int initializations = 0;

static int next_value(void) {
    initializations = initializations + 1;
    return initializations;
}

static int read_file_scope(void) {
    return file_scope;
}

static void bump_file_scope(void) {
    file_scope = file_scope + 1;
}

static int lazy(void) {
    _Thread_local int value = next_value();
    return value;
}

static void never_called(void) {
    _Thread_local int value = next_value();
    (void)value;
}

int main(void) {
    if (read_file_scope() != 0) {
        return 1;
    }
    bump_file_scope();
    bump_file_scope();
    if (read_file_scope() != 2) {
        return 2;
    }

    // A never-called function's thread-local object is not initialized.
    if (initializations != 0) {
        return 3;
    }

    // The first use initializes, and the second does not.
    if (lazy() != 1) {
        return 4;
    }
    if (lazy() != 1) {
        return 5;
    }
    if (initializations != 1) {
        return 6;
    }

    // The file-scope object is unaffected by the block-scope one.
    if (read_file_scope() != 2) {
        return 7;
    }

    return 42;
}
