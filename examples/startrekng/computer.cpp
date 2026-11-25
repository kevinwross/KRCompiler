#include <stdio.h>

#include "computer.hpp"
#include "game.hpp"
#include "geometry.hpp"
#include "ship.hpp"

bool LibraryComputer::available(Game& game) {
    if (game.ship().damaged(DEV_COMPUTER)) {
        game.console().line("Library computer inoperable.");
        return false;
    }
    return true;
}

void LibraryComputer::run(Game& game) {
    if (!available(game)) {
        return;
    }
    game.console().line("Computer options:");
    game.console().line("  0 = galactic record   1 = status report");
    game.console().line("  2 = torpedo data      3 = starbase data");
    game.console().line("  4 = direction/distance 5 = galaxy map");
    int option = 0;
    if (!game.console().readNumber("Option: ", 0, 5, &option)) {
        return;
    }
    if (option == 0) {
        galacticRecord(game);
    } else if (option == 1) {
        statusReport(game);
    } else if (option == 2) {
        torpedoData(game);
    } else if (option == 3) {
        starbaseData(game);
    } else if (option == 4) {
        directionDistance(game);
    } else {
        galaxyMap(game);
    }
}

void LibraryComputer::galacticRecord(Game& game) {
    Galaxy& galaxy = game.galaxy();
    game.console().line("Computer record of galaxy");
    game.console().line("     1     2     3     4     5     6     7     8");
    for (int qx = 1; qx <= 8; ++qx) {
        game.console().line("   ----- ----- ----- ----- ----- ----- ----- -----");
        printf("%d", qx);
        for (int qy = 1; qy <= 8; ++qy) {
            if (galaxy.isKnown(qx, qy)) {
                printf("   %3d", galaxy.quadrantCode(qx, qy));
            } else {
                printf("   ***");
            }
        }
        printf("\n");
    }
    game.console().line("   ----- ----- ----- ----- ----- ----- ----- -----");
}

void LibraryComputer::statusReport(Game& game) {
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    game.console().line("Status report");
    printf("Klingons left: %d\n", galaxy.klingonsLeft());
    printf("Stardates remaining: %d\n", ship.endStardate - ship.stardate);
    printf("Starbases left: %d\n", galaxy.starbasesLeft());
    printf("Energy: %d   Shields: %d   Torpedoes: %d\n",
           ship.energy, ship.shields, ship.torpedoes);
    for (int i = 1; i <= 8; ++i) {
        if (ship.damage_[i] < 0) {
            printf("  %s: damaged\n", deviceName(i));
        }
    }
}

void LibraryComputer::torpedoData(Game& game) {
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    int klingons = galaxy.klingonsHere(ship.qx, ship.qy);
    if (klingons == 0) {
        game.console().line("Sensors show no Klingon ships in this quadrant.");
        return;
    }
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (galaxy.object(ship.qx, ship.qy, x + 1, y + 1) == ST_KLINGON) {
                printf("Klingon at sector %d,%d\n", x + 1, y + 1);
                bearing(game, x + 1, y + 1);
            }
        }
    }
}

void LibraryComputer::starbaseData(Game& game) {
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (galaxy.object(ship.qx, ship.qy, x + 1, y + 1) == ST_BASE) {
                printf("Starbase at sector %d,%d\n", x + 1, y + 1);
                bearing(game, x + 1, y + 1);
                return;
            }
        }
    }
    game.console().line("Sensors show no starbase in this quadrant.");
}

void LibraryComputer::directionDistance(Game& game) {
    int x1 = 1;
    int y1 = 1;
    int x2 = 1;
    int y2 = 1;
    if (!game.console().readNumber("Initial X: ", 1, 8, &x1) ||
        !game.console().readNumber("Initial Y: ", 1, 8, &y1) ||
        !game.console().readNumber("Final X: ", 1, 8, &x2) ||
        !game.console().readNumber("Final Y: ", 1, 8, &y2)) {
        return;
    }
    printf("Direction %d, distance %d\n",
           stCourse(x1, y1, x2, y2), stDistance(x1, y1, x2, y2));
}

void LibraryComputer::galaxyMap(Game& game) {
    printf("\n                   The Galaxy\n\n");
    printf("    1     2     3     4     5     6     7     8\n");
    for (int qx = 1; qx <= 8; ++qx) {
        printf("  ----- ----- ----- ----- ----- ----- ----- -----\n");
        printf("%d ", qx);
        for (int qy = 1; qy <= 8; ++qy) {
            char name[32];
            game.galaxy().regionName(qx, qy, name, 32);
            printf(" %-12s", name);
        }
        printf("\n");
    }
    printf("  ----- ----- ----- ----- ----- ----- ----- -----\n\n");
}

void LibraryComputer::bearing(Game& game, int x, int y) {
    Ship& ship = game.ship();
    printf("  Direction %d, distance %d\n",
           stCourse(ship.sx, ship.sy, x, y),
           stDistance(ship.sx, ship.sy, x, y));
}
