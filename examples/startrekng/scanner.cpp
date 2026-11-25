#include <stdio.h>

#include "game.hpp"
#include "scanner.hpp"
#include "ship.hpp"

static bool insideGalaxy(int value) {
    return value >= 1 && value <= ST_GALAXY;
}

bool ShortRangeScanner::available(Game& game) {
    if (game.ship().damaged(DEV_SRS)) {
        game.console().line("*** Short range sensors are out ***");
        return false;
    }
    return true;
}

void ShortRangeScanner::scan(Game& game) {
    if (!available(game)) {
        return;
    }
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    int klingons = galaxy.klingonsHere(ship.qx, ship.qy);

    const char* condition = "GREEN";
    if (ship.docked) {
        condition = "DOCKED";
    } else if (klingons > 0) {
        condition = "*RED*";
    } else if (ship.energy < 300) {
        condition = "YELLOW";
    }

    printf("------------------------\n");
    for (int x = 0; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            if (x + 1 == ship.sx && y + 1 == ship.sy) {
                printf("<*>");
            } else {
                int code = galaxy.object(ship.qx, ship.qy, x + 1, y + 1);
                if (code == ST_KLINGON) {
                    printf("+K+");
                } else if (code == ST_BASE) {
                    printf(">!<");
                } else if (code == ST_STAR) {
                    printf(" * ");
                } else {
                    printf("   ");
                }
            }
        }
        printf("\n");
    }
    printf("------------------------\n");
    printf("Stardate %d   Condition %s\n", ship.stardate, condition);
    printf("Quadrant %d,%d   Sector %d,%d\n", ship.qx, ship.qy, ship.sx, ship.sy);
    printf("Photon torpedoes %d   Total energy %d   Shields %d\n",
           ship.torpedoes, ship.energy + ship.shields, ship.shields);
    printf("Klingons remaining %d\n", galaxy.klingonsLeft());
}

bool LongRangeScanner::available(Game& game) {
    if (game.ship().damaged(DEV_LRS)) {
        game.console().line("Long range sensors are inoperable.");
        return false;
    }
    return true;
}

void LongRangeScanner::scan(Game& game) {
    if (!available(game)) {
        return;
    }
    Ship& ship = game.ship();
    Galaxy& galaxy = game.galaxy();
    printf("Long range scan for quadrant %d,%d\n", ship.qx, ship.qy);
    printf("-----------------\n");
    for (int qx = ship.qx - 1; qx <= ship.qx + 1; ++qx) {
        printf(":");
        for (int qy = ship.qy - 1; qy <= ship.qy + 1; ++qy) {
            if (insideGalaxy(qx) && insideGalaxy(qy)) {
                galaxy.markKnown(qx, qy);
                printf(" %3d :", galaxy.quadrantCode(qx, qy));
            } else {
                printf(" *** :");
            }
        }
        printf("\n-----------------\n");
    }
    printf("\n");
}
