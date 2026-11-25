#ifndef STARTREKNG_CONSOLE_HPP
#define STARTREKNG_CONSOLE_HPP

/*
 * Console is the only place the game touches stdio. Every other class takes a
 * Console& so input and output stay behind one interface.
 */
class Console {
public:
    Console();

    /* Replaces the console with a script of at most capacity bytes. */
    void setScript(const char* text, int length);

    void text(const char* value);
    void line(const char* value);
    void prompt(const char* value);

    /* Reads one line, without its newline. False means end of input. */
    bool readLine(char* buffer, int capacity);

    /* Prompts and reads an integer in [low, high]. False on EOF or bad input. */
    bool readNumber(const char* promptText, int low, int high, int* value);

    static int lower(int c);
    static bool startsWith3(const char* text, const char* word);

private:
    bool parseInteger(const char* text, int* value) const;

    const char* script_;
    int scriptLength_;
    int scriptPosition_;
};

#endif
