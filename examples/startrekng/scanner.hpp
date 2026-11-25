#ifndef STARTREKNG_SCANNER_HPP
#define STARTREKNG_SCANNER_HPP

class Game;

/*
 * A scanner is a device the game asks for a report. The two implementations
 * differ in range and in what they reveal; Game never branches on the kind.
 */
class Scanner {
public:
    virtual ~Scanner() {
    }

    virtual bool available(Game& game) = 0;
    virtual void scan(Game& game) = 0;
};

class ShortRangeScanner : public Scanner {
public:
    bool available(Game& game);
    void scan(Game& game);
};

class LongRangeScanner : public Scanner {
public:
    bool available(Game& game);
    void scan(Game& game);
};

#endif
