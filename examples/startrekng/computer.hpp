#ifndef STARTREKNG_COMPUTER_HPP
#define STARTREKNG_COMPUTER_HPP

class Game;

/*
 * The library computer is the ship's report interface. Game invokes it as a
 * Computer; the individual reports stay behind it.
 */
class Computer {
public:
    virtual ~Computer() {
    }

    virtual bool available(Game& game) = 0;
    virtual void run(Game& game) = 0;
};

class LibraryComputer : public Computer {
public:
    bool available(Game& game);
    void run(Game& game);

private:
    void galacticRecord(Game& game);
    void statusReport(Game& game);
    void torpedoData(Game& game);
    void starbaseData(Game& game);
    void directionDistance(Game& game);
    void galaxyMap(Game& game);
    void bearing(Game& game, int x, int y);
};

#endif
