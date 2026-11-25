/*
 * startrek.cpp - a text-mode Star Trek game for krc.
 *
 * This is a single-file homage to Super Star Trek, the 1978 BASIC game by Bob
 * Leedom (from Mike Mayfield's original), published in David Ahl's BASIC
 * Computer Games. It is an independent implementation: the 8x8 galaxy, the
 * Klingon/starbase/star quadrants, the eight ship systems, the library computer,
 * and the course/warp/phaser/torpedo/shield commands are the game's rules, not
 * a copy of any listing.
 *
 * The galaxy is generated from a fixed seed, so a scripted session is
 * reproducible. Commands are three-letter prefixes: nav, srs, lrs, pha, tor,
 * she, dam, com, xxx. `q` leaves the game. Numeric prompts accept one number
 * per line; end of input exits cleanly.
 *
 * Build for KR32 (from the repository root):
 *   krc -c examples/startrek.cpp -o build/startrek.kro
 *   krld build/startrek.kro build/windows-msvc-debug/stage/sysroot/kr32/lib/libkrcrt.kro \
 *        targets/kr32/runtime/crt0.s targets/kr32/runtime/fileio.s \
 *        targets/kr32/runtime/eh.s -o build/startrek.kr32
 *
 * Run:
 *   ..\kr32\build\windows-msvc\Debug\kr32.exe vm build/startrek.kr32 \
 *       --max-steps 50000000 --input "srs`nlrs`nxxx`n"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int GALAXY = 8;
static const int EMPTY = 0;
static const int STAR = 1;
static const int KLINGON = 2;
static const int BASE = 3;

/* Quadrant objects, indexed [quadrant][sector x][sector y]. Flattening the two
 * quadrant coordinates keeps every element stride a power of two, which the
 * KR232 backend requires. kslot is the Klingon's 0..2 slot, or -1. khp uses a
 * fourth padding slot for the same reason. */
static unsigned char objects[64][8][8];
static signed char kslot[64][8][8];
static int khp[64][4];
static unsigned char known[64];

static int quadrant_index(int qx, int qy) {
    return (qx - 1) * GALAXY + (qy - 1);
}

static int q1;
static int q2;
static int s1;
static int s2;
static int energy;
static int shields;
static int torpedoes;
static int klingons_left;
static int starbases_left;
static int initial_klingons;
static int stardate;
static int start_stardate;
static int end_stardate;

/* 1 warp, 2 short range, 3 long range, 4 phasers, 5 torpedoes,
 * 6 damage control, 7 shields, 8 computer. 0 is operational, negative is
 * damaged; one point is repaired per turn when damage control works. */
static int damage[9];
static int docked;
static int game_over;
static int won;

static const int dx[9] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
static const int dy[9] = {0, -1, -1, 0, 1, 1, 1, 0, -1};

static const char *const device_names[9] = {
    "", "Warp Engines", "Short Range Sensors", "Long Range Sensors",
    "Phaser Control", "Photon Tubes", "Damage Control", "Shield Control",
    "Library Computer"
};

static const char *const row_names[8] = {
    "Antares", "Rigel", "Procyon", "Vega",
    "Canopus", "Altair", "Sagittarius", "Pollux"
};

static const char *const sector_names[4] = {"", " II", " III", " IV"};

static int lower(int c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

static int read_line(char *buffer, int capacity) {
    int count = 0;
    int c = getchar();
    if (c < 0) {
        buffer[0] = 0;
        return 0;
    }
    while (c >= 0 && c != '\n' && c != '\r') {
        if (count + 1 < capacity) {
            buffer[count++] = (char)c;
        }
        c = getchar();
    }
    buffer[count] = 0;
    return 1;
}

static int prefix3(const char *text, const char *word) {
    for (int i = 0; i < 3; ++i) {
        if (text[i] == 0 || lower(text[i]) != lower(word[i])) {
            return 0;
        }
    }
    return 1;
}

/* Parses a decimal integer with optional surrounding spaces. Returns 0 when
 * the text is not a complete integer. */
static int parse_int(const char *text, int *value) {
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    int sign = 1;
    if (*text == '-') {
        sign = -1;
        ++text;
    } else if (*text == '+') {
        ++text;
    }
    if (*text < '0' || *text > '9') {
        return 0;
    }
    int result = 0;
    while (*text >= '0' && *text <= '9') {
        result = result * 10 + (*text - '0');
        ++text;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    if (*text != 0) {
        return 0;
    }
    *value = sign * result;
    return 1;
}

/* Reads one integer in [low, high]. Returns 0 on EOF or invalid input. */
static int read_number(const char *prompt, int low, int high, int *value) {
    printf("%s", prompt);
    char line[64];
    if (!read_line(line, sizeof(line))) {
        return 0;
    }
    int parsed = 0;
    if (!parse_int(line, &parsed) || parsed < low || parsed > high) {
        printf("Expected a number from %d to %d.\n", low, high);
        return 0;
    }
    *value = parsed;
    return 1;
}

static int inside(int value) {
    return value >= 1 && value <= GALAXY;
}

static int base_near(int x, int y) {
    for (int i = x - 1; i <= x + 1; ++i) {
        for (int j = y - 1; j <= y + 1; ++j) {
            if (inside(i) && inside(j) && objects[quadrant_index(q1, q2)][i - 1][j - 1] == BASE) {
                return 1;
            }
        }
    }
    return 0;
}

static int klingons_here(void) {
    int count = 0;
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            if (objects[quadrant_index(q1, q2)][i][j] == KLINGON) {
                ++count;
            }
        }
    }
    return count;
}

static int stars_here(void) {
    int count = 0;
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            if (objects[quadrant_index(q1, q2)][i][j] == STAR) {
                ++count;
            }
        }
    }
    return count;
}

static int quadrant_code(int qx, int qy) {
    int klingons = 0;
    int bases = 0;
    int stars = 0;
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            int code = objects[quadrant_index(qx, qy)][i][j];
            if (code == KLINGON) {
                ++klingons;
            } else if (code == BASE) {
                ++bases;
            } else if (code == STAR) {
                ++stars;
            }
        }
    }
    return klingons * 100 + bases * 10 + stars;
}

static void clear_quadrant(int qx, int qy) {
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            objects[quadrant_index(qx, qy)][i][j] = EMPTY;
            kslot[quadrant_index(qx, qy)][i][j] = -1;
        }
    }
    for (int i = 0; i < 4; ++i) {
        khp[quadrant_index(qx, qy)][i] = 0;
    }
}

static int place_random(int qx, int qy, int code, int slot) {
    for (int attempt = 0; attempt < 100; ++attempt) {
        int x = rand() % GALAXY;
        int y = rand() % GALAXY;
        if (objects[quadrant_index(qx, qy)][x][y] == EMPTY) {
            objects[quadrant_index(qx, qy)][x][y] = (unsigned char)code;
            kslot[quadrant_index(qx, qy)][x][y] = (signed char)slot;
            return 1;
        }
    }
    for (int x = 0; x < GALAXY; ++x) {
        for (int y = 0; y < GALAXY; ++y) {
            if (objects[quadrant_index(qx, qy)][x][y] == EMPTY) {
                objects[quadrant_index(qx, qy)][x][y] = (unsigned char)code;
                kslot[quadrant_index(qx, qy)][x][y] = (signed char)slot;
                return 1;
            }
        }
    }
    return 0;
}

static void generate_galaxy(void) {
    klingons_left = 0;
    starbases_left = 0;
    for (int qx = 1; qx <= GALAXY; ++qx) {
        for (int qy = 1; qy <= GALAXY; ++qy) {
            clear_quadrant(qx, qy);
            int roll = rand() % 100;
            int klingons = 0;
            if (roll > 98) {
                klingons = 3;
            } else if (roll > 95) {
                klingons = 2;
            } else if (roll > 80) {
                klingons = 1;
            }
            int base = rand() % 100 > 96 ? 1 : 0;
            int stars = rand() % 8;
            for (int i = 0; i < klingons; ++i) {
                if (place_random(qx, qy, KLINGON, i)) {
                    khp[quadrant_index(qx, qy)][i] = 100 + rand() % 200;
                    ++klingons_left;
                }
            }
            if (base && place_random(qx, qy, BASE, -1)) {
                ++starbases_left;
            }
            for (int i = 0; i < stars; ++i) {
                place_random(qx, qy, STAR, -1);
            }
        }
    }
    if (starbases_left == 0) {
        int qx = 1 + rand() % GALAXY;
        int qy = 1 + rand() % GALAXY;
        if (place_random(qx, qy, BASE, -1)) {
            ++starbases_left;
        }
    }
}

static void region_name(int qx, int qy, char *buffer, int capacity) {
    int index = (qx - 1) * 2 + (qy > 4 ? 1 : 0);
    const char *name = row_names[index % 8];
    const char *suffix = sector_names[(qy - 1) % 4];
    int used = 0;
    for (int i = 0; name[i] != 0 && used + 1 < capacity; ++i) {
        buffer[used++] = name[i];
    }
    for (int i = 0; suffix[i] != 0 && used + 1 < capacity; ++i) {
        buffer[used++] = suffix[i];
    }
    buffer[used] = 0;
}

static int distance_between(int x1, int y1, int x2, int y2) {
    int ax = x1 > x2 ? x1 - x2 : x2 - x1;
    int ay = y1 > y2 ? y1 - y2 : y2 - y1;
    return ax > ay ? ax : ay;
}

static int course_between(int x1, int y1, int x2, int y2) {
    int ddx = x2 - x1;
    int ddy = y2 - y1;
    int ax = ddx < 0 ? -ddx : ddx;
    int ay = ddy < 0 ? -ddy : ddy;
    if (ddx == 0 && ddy == 0) {
        return 0;
    }
    if (ddx == 0) {
        return ddy < 0 ? 1 : 5;
    }
    if (ddy == 0) {
        return ddx > 0 ? 3 : 7;
    }
    if (ax > ay * 2) {
        return ddx > 0 ? 3 : 7;
    }
    if (ay > ax * 2) {
        return ddy < 0 ? 1 : 5;
    }
    if (ddx > 0) {
        return ddy < 0 ? 2 : 4;
    }
    return ddy < 0 ? 8 : 6;
}

static void print_bearing(int x, int y) {
    int course = course_between(s1, s2, x, y);
    int distance = distance_between(s1, s2, x, y);
    printf("  Direction %d, distance %d\n", course, distance);
}

static void dock(void) {
    if (docked) {
        return;
    }
    docked = 1;
    energy = 3000;
    shields = 0;
    torpedoes = 10;
    for (int i = 1; i <= 8; ++i) {
        damage[i] = 0;
    }
    printf("Starbase docking complete. Energy and torpedoes replenished;\n");
    printf("all systems repaired and shields dropped.\n");
}

static void check_docking(void) {
    if (base_near(s1, s2)) {
        dock();
    } else {
        docked = 0;
    }
}

static void enter_quadrant(int first) {
    known[quadrant_index(q1, q2)] = 1;
    docked = 0;
    char name[32];
    region_name(q1, q2, name, sizeof(name));
    int klingons = klingons_here();
    if (first) {
        printf("Your mission begins in the %s quadrant.\n", name);
    } else {
        printf("Now entering the %s quadrant.\n", name);
    }
    if (klingons > 0) {
        printf("Combat area: condition red.\n");
        if (shields < 200) {
            printf("Shields are dangerously low.\n");
        }
    }
}

static void short_range_scan(void) {
    if (damage[2] < 0) {
        printf("*** Short range sensors are out ***\n");
        return;
    }
    int klingons = klingons_here();
    const char *condition = "GREEN";
    if (docked) {
        condition = "DOCKED";
    } else if (klingons > 0) {
        condition = "*RED*";
    } else if (energy < 300) {
        condition = "YELLOW";
    }

    printf("------------------------\n");
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            if (i + 1 == s1 && j + 1 == s2) {
                printf("<*>");
            } else {
                int code = objects[quadrant_index(q1, q2)][i][j];
                if (code == KLINGON) {
                    printf("+K+");
                } else if (code == BASE) {
                    printf(">!<");
                } else if (code == STAR) {
                    printf(" * ");
                } else {
                    printf("   ");
                }
            }
        }
        printf("\n");
    }
    printf("------------------------\n");
    printf("Stardate %d   Condition %s\n", stardate, condition);
    printf("Quadrant %d,%d   Sector %d,%d\n", q1, q2, s1, s2);
    printf("Photon torpedoes %d   Total energy %d   Shields %d\n",
           torpedoes, energy + shields, shields);
    printf("Klingons remaining %d\n", klingons_left);
}

static void long_range_scan(void) {
    if (damage[3] < 0) {
        printf("Long range sensors are inoperable.\n");
        return;
    }
    printf("Long range scan for quadrant %d,%d\n", q1, q2);
    printf("-----------------\n");
    for (int i = q1 - 1; i <= q1 + 1; ++i) {
        printf(":");
        for (int j = q2 - 1; j <= q2 + 1; ++j) {
            if (inside(i) && inside(j)) {
                known[quadrant_index(i, j)] = 1;
                printf(" %3d :", quadrant_code(i, j));
            } else {
                printf(" *** :");
            }
        }
        printf("\n-----------------\n");
    }
    printf("\n");
}

static void galactic_record(void) {
    printf("Computer record of galaxy\n");
    printf("     1     2     3     4     5     6     7     8\n");
    for (int i = 1; i <= GALAXY; ++i) {
        printf("   ----- ----- ----- ----- ----- ----- ----- -----\n");
        printf("%d", i);
        for (int j = 1; j <= GALAXY; ++j) {
            if (known[quadrant_index(i, j)]) {
                printf("   %3d", quadrant_code(i, j));
            } else {
                printf("   ***");
            }
        }
        printf("\n");
    }
    printf("   ----- ----- ----- ----- ----- ----- ----- -----\n");
}

static void status_report(void) {
    printf("Status report\n");
    printf("Klingons left: %d\n", klingons_left);
    printf("Stardates remaining: %d\n", end_stardate - stardate);
    printf("Starbases left: %d\n", starbases_left);
    printf("Energy: %d   Shields: %d   Torpedoes: %d\n",
           energy, shields, torpedoes);
    for (int i = 1; i <= 8; ++i) {
        if (damage[i] < 0) {
            printf("  %s: damaged\n", device_names[i]);
        }
    }
}

static void torpedo_data(void) {
    int klingons = klingons_here();
    if (klingons == 0) {
        printf("Sensors show no Klingon ships in this quadrant.\n");
        return;
    }
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            if (objects[quadrant_index(q1, q2)][i][j] == KLINGON) {
                printf("Klingon at sector %d,%d\n", i + 1, j + 1);
                print_bearing(i + 1, j + 1);
            }
        }
    }
}

static void starbase_data(void) {
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            if (objects[quadrant_index(q1, q2)][i][j] == BASE) {
                printf("Starbase at sector %d,%d\n", i + 1, j + 1);
                print_bearing(i + 1, j + 1);
                return;
            }
        }
    }
    printf("Sensors show no starbase in this quadrant.\n");
}

static void direction_distance(void) {
    int x1 = 1;
    int y1 = 1;
    int x2 = 1;
    int y2 = 1;
    if (!read_number("Initial X: ", 1, GALAXY, &x1) ||
        !read_number("Initial Y: ", 1, GALAXY, &y1) ||
        !read_number("Final X: ", 1, GALAXY, &x2) ||
        !read_number("Final Y: ", 1, GALAXY, &y2)) {
        return;
    }
    printf("Direction %d, distance %d\n",
           course_between(x1, y1, x2, y2),
           distance_between(x1, y1, x2, y2));
}

static void galaxy_map(void) {
    printf("\n                   The Galaxy\n\n");
    printf("    1     2     3     4     5     6     7     8\n");
    for (int i = 1; i <= GALAXY; ++i) {
        printf("  ----- ----- ----- ----- ----- ----- ----- -----\n");
        printf("%d ", i);
        for (int j = 1; j <= GALAXY; ++j) {
            char name[32];
            region_name(i, j, name, sizeof(name));
            printf(" %-12s", name);
        }
        printf("\n");
    }
    printf("  ----- ----- ----- ----- ----- ----- ----- -----\n\n");
}

static void library_computer(void) {
    if (damage[8] < 0) {
        printf("Library computer inoperable.\n");
        return;
    }
    printf("Computer options:\n");
    printf("  0 = galactic record   1 = status report\n");
    printf("  2 = torpedo data      3 = starbase data\n");
    printf("  4 = direction/distance 5 = galaxy map\n");
    int option = 0;
    if (!read_number("Option: ", 0, 5, &option)) {
        return;
    }
    if (option == 0) {
        galactic_record();
    } else if (option == 1) {
        status_report();
    } else if (option == 2) {
        torpedo_data();
    } else if (option == 3) {
        starbase_data();
    } else if (option == 4) {
        direction_distance();
    } else {
        galaxy_map();
    }
}

static void repair_damage(void) {
    if (damage[6] < 0) {
        return;
    }
    for (int i = 1; i <= 8; ++i) {
        if (damage[i] < 0) {
            ++damage[i];
            if (damage[i] >= 0) {
                damage[i] = 0;
                printf("%s repair completed.\n", device_names[i]);
            }
        }
    }
}

static void damage_control(void) {
    printf("Device                 State\n");
    for (int i = 1; i <= 8; ++i) {
        printf("%-22s %s\n", device_names[i],
               damage[i] < 0 ? "damaged" : "operational");
    }
    if (damage[6] < 0) {
        printf("Damage control is damaged; no repairs are underway.\n");
    }
}

static void destroy_enterprise(const char *reason) {
    printf("%s\n", reason);
    printf("The Enterprise has been destroyed. The Federation will be conquered.\n");
    game_over = 1;
    won = 0;
}

static void klingons_shoot(void) {
    if (docked) {
        printf("Starbase shields protect the Enterprise.\n");
        return;
    }
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            if (objects[quadrant_index(q1, q2)][i][j] != KLINGON) {
                continue;
            }
            int slot = kslot[quadrant_index(q1, q2)][i][j];
            if (slot < 0 || slot > 2) {
                continue;
            }
            int distance = distance_between(s1, s2, i + 1, j + 1);
            if (distance < 1) {
                distance = 1;
            }
            int hit = khp[quadrant_index(q1, q2)][slot] * (2 + rand() % 2) / distance;
            if (hit < 1) {
                hit = 1;
            }
            if (shields <= 0) {
                destroy_enterprise("A Klingon hit the undefended hull.");
                return;
            }
            shields -= hit;
            printf("%d unit hit on Enterprise from sector %d,%d\n",
                   hit, i + 1, j + 1);
            if (shields <= 0) {
                shields = 0;
                destroy_enterprise("The shields have failed.");
                return;
            }
            printf("  Shields down to %d units.\n", shields);
            if (hit >= 20 && (rand() % 100 < 60 || hit > shields / 5)) {
                int device = 1 + rand() % 8;
                int amount = 1 + rand() % 3;
                damage[device] -= amount;
                printf("Damage Control reports: %s damaged.\n",
                       device_names[device]);
            }
        }
    }
}

static void destroy_klingon(int qx, int qy, int x, int y) {
    int slot = kslot[quadrant_index(qx, qy)][x][y];
    objects[quadrant_index(qx, qy)][x][y] = EMPTY;
    kslot[quadrant_index(qx, qy)][x][y] = -1;
    if (slot >= 0 && slot <= 2) {
        khp[quadrant_index(qx, qy)][slot] = 0;
    }
    --klingons_left;
    printf("*** Klingon at sector %d,%d destroyed ***\n", x + 1, y + 1);
}

static void check_time(void) {
    if (stardate > end_stardate) {
        printf("Starfleet Command has declared the mission a failure.\n");
        printf("It is stardate %d; the deadline was %d.\n",
               stardate, end_stardate);
        game_over = 1;
        won = 0;
    }
}

static void win_game(void) {
    printf("Congratulations, Captain. The last Klingon battle cruiser\n");
    printf("menacing the Federation has been destroyed.\n");
    int elapsed = stardate - start_stardate;
    if (elapsed < 1) {
        elapsed = 1;
    }
    int rating = 1000 * initial_klingons * initial_klingons /
                 (elapsed * elapsed);
    printf("Efficiency rating: %d\n", rating);
    game_over = 1;
    won = 1;
}

static void phasers(void) {
    if (damage[4] < 0) {
        printf("Phasers inoperative.\n");
        return;
    }
    int klingons = klingons_here();
    if (klingons == 0) {
        printf("Sensors show no enemy ships in this quadrant.\n");
        return;
    }
    printf("Energy available: %d\n", energy);
    int fire = 0;
    if (!read_number("Units to fire: ", 1, energy, &fire)) {
        return;
    }
    energy -= fire;
    int per_target = fire / klingons;
    for (int i = 0; i < GALAXY; ++i) {
        for (int j = 0; j < GALAXY; ++j) {
            if (objects[quadrant_index(q1, q2)][i][j] != KLINGON) {
                continue;
            }
            int slot = kslot[quadrant_index(q1, q2)][i][j];
            if (slot < 0 || slot > 2 || khp[quadrant_index(q1, q2)][slot] <= 0) {
                continue;
            }
            int distance = distance_between(s1, s2, i + 1, j + 1);
            if (distance < 1) {
                distance = 1;
            }
            int hit = per_target * (2 + rand() % 2) / distance;
            if (damage[8] < 0) {
                hit /= 2;
            }
            if (hit < 1) {
                hit = 1;
            }
            khp[quadrant_index(q1, q2)][slot] -= hit;
            printf("%d unit hit on Klingon at sector %d,%d\n",
                   hit, i + 1, j + 1);
            if (khp[quadrant_index(q1, q2)][slot] <= 0) {
                destroy_klingon(q1, q2, i, j);
            } else {
                printf("  (%d units remaining)\n", khp[quadrant_index(q1, q2)][slot]);
            }
        }
    }
    if (klingons_left <= 0) {
        win_game();
        return;
    }
    klingons_shoot();
}

static void photon_torpedoes(void) {
    if (damage[5] < 0) {
        printf("Photon tubes are not operational.\n");
        return;
    }
    if (torpedoes <= 0) {
        printf("All photon torpedoes have been expended.\n");
        return;
    }
    if (energy < 2) {
        printf("Insufficient energy to launch a torpedo.\n");
        return;
    }
    int course = 0;
    if (!read_number("Course (1-8): ", 1, 8, &course)) {
        return;
    }
    --torpedoes;
    energy -= 2;
    int x = s1;
    int y = s2;
    int tq1 = q1;
    int tq2 = q2;
    printf("Torpedo track:\n");
    for (;;) {
        x += dx[course];
        y += dy[course];
        if (x < 1 || x > GALAXY || y < 1 || y > GALAXY) {
            int nq1 = tq1;
            int nq2 = tq2;
            if (x < 1) {
                --nq1;
                x = GALAXY;
            } else if (x > GALAXY) {
                ++nq1;
                x = 1;
            }
            if (y < 1) {
                --nq2;
                y = GALAXY;
            } else if (y > GALAXY) {
                ++nq2;
                y = 1;
            }
            if (!inside(nq1) || !inside(nq2)) {
                printf("Torpedo missed.\n");
                break;
            }
            tq1 = nq1;
            tq2 = nq2;
            known[quadrant_index(tq1, tq2)] = 1;
        }
        printf("  %d,%d\n", x, y);
        int code = objects[quadrant_index(tq1, tq2)][x - 1][y - 1];
        if (code == KLINGON) {
            destroy_klingon(tq1, tq2, x - 1, y - 1);
            break;
        }
        if (code == STAR) {
            printf("Star at sector %d,%d absorbed the torpedo.\n", x, y);
            break;
        }
        if (code == BASE) {
            objects[quadrant_index(tq1, tq2)][x - 1][y - 1] = EMPTY;
            --starbases_left;
            printf("*** Starbase at sector %d,%d destroyed ***\n", x, y);
            printf("Starfleet Command is reviewing your record for court martial.\n");
            break;
        }
    }
    if (klingons_left <= 0) {
        win_game();
        return;
    }
    klingons_shoot();
}

static void shield_control(void) {
    if (damage[7] < 0) {
        printf("Shield control inoperable.\n");
        return;
    }
    int available = energy + shields;
    printf("Energy available: %d\n", available);
    int value = 0;
    if (!read_number("Units to shields: ", 0, available - 1, &value)) {
        return;
    }
    energy = available - value;
    shields = value;
    printf("Shields now at %d units; engines at %d units.\n", shields, energy);
}

static void navigate(void) {
    int course = 0;
    if (!read_number("Course (1-8): ", 1, 8, &course)) {
        return;
    }
    int warp = 0;
    if (!read_number("Warp factor (0-8): ", 0, 8, &warp)) {
        return;
    }
    if (warp == 0) {
        printf("Helm answers: no movement ordered.\n");
        return;
    }
    docked = 0;
    if (damage[1] < 0 && warp > 1) {
        printf("Warp engines are damaged; maximum speed is warp 1.\n");
        return;
    }
    int steps = warp * 8;
    int cost = steps + 10;
    if (energy < cost) {
        printf("Insufficient energy for that maneuver.\n");
        return;
    }
    energy -= cost;
    int previous_q1 = q1;
    int previous_q2 = q2;
    for (int step = 0; step < steps; ++step) {
        int nx = s1 + dx[course];
        int ny = s2 + dy[course];
        int nq1 = q1;
        int nq2 = q2;
        if (nx < 1) {
            --nq1;
            nx = GALAXY;
        } else if (nx > GALAXY) {
            ++nq1;
            nx = 1;
        }
        if (ny < 1) {
            --nq2;
            ny = GALAXY;
        } else if (ny > GALAXY) {
            ++nq2;
            ny = 1;
        }
        if (!inside(nq1) || !inside(nq2)) {
            printf("Starfleet Command: permission to cross the galactic\n");
            printf("perimeter is denied. Engines shut down.\n");
            break;
        }
        if (objects[quadrant_index(nq1, nq2)][nx - 1][ny - 1] != EMPTY) {
            printf("Warp engines shut down at sector %d,%d due to bad navigation.\n",
                   s1, s2);
            break;
        }
        q1 = nq1;
        q2 = nq2;
        s1 = nx;
        s2 = ny;
        if (q1 != previous_q1 || q2 != previous_q2) {
            enter_quadrant(0);
            previous_q1 = q1;
            previous_q2 = q2;
        }
    }
    check_docking();
    ++stardate;
    repair_damage();
    if (!game_over && klingons_here() > 0) {
        klingons_shoot();
    }
    if (!game_over) {
        check_time();
    }
    if (!game_over) {
        short_range_scan();
    }
}

static void resign(void) {
    printf("You have resigned your commission.\n");
    printf("There were %d Klingon battle cruisers left.\n", klingons_left);
    game_over = 1;
    won = 0;
}

static void initialize(void) {
    srand(1);
    game_over = 0;
    won = 0;
    stardate = 3000;
    start_stardate = stardate;
    end_stardate = stardate + 25 + rand() % 10;
    energy = 3000;
    shields = 0;
    torpedoes = 10;
    docked = 0;
    for (int i = 1; i <= 8; ++i) {
        damage[i] = 0;
    }
    for (int i = 0; i < 64; ++i) {
        known[i] = 0;
    }
    generate_galaxy();
    initial_klingons = klingons_left;
    q1 = 1 + rand() % GALAXY;
    q2 = 1 + rand() % GALAXY;
    for (;;) {
        s1 = 1 + rand() % GALAXY;
        s2 = 1 + rand() % GALAXY;
        if (objects[quadrant_index(q1, q2)][s1 - 1][s2 - 1] == EMPTY) {
            break;
        }
    }
}

static void intro(void) {
    printf("Super Star Trek\n");
    printf("A single-file homage to the 1978 BASIC game.\n\n");
    printf("Your orders are to destroy the %d Klingon warships which have\n",
           klingons_left);
    printf("invaded the galaxy before they can attack Federation Headquarters\n");
    printf("on stardate %d. This gives you %d stardates.\n",
           end_stardate, end_stardate - stardate);
    if (starbases_left == 1) {
        printf("There is 1 starbase in the galaxy for resupplying your ship.\n\n");
    } else {
        printf("There are %d starbases in the galaxy for resupplying your ship.\n\n",
               starbases_left);
    }
}

int main(void) {
    initialize();
    intro();
    enter_quadrant(1);
    check_docking();
    short_range_scan();

    while (!game_over) {
        printf("\nCommand? ");
        char line[64];
        if (!read_line(line, sizeof(line))) {
            printf("\nInput ended.\n");
            break;
        }
        if (prefix3(line, "nav")) {
            navigate();
        } else if (prefix3(line, "srs")) {
            short_range_scan();
        } else if (prefix3(line, "lrs")) {
            long_range_scan();
        } else if (prefix3(line, "pha")) {
            phasers();
        } else if (prefix3(line, "tor")) {
            photon_torpedoes();
        } else if (prefix3(line, "she")) {
            shield_control();
        } else if (prefix3(line, "dam")) {
            damage_control();
        } else if (prefix3(line, "com")) {
            library_computer();
        } else if (prefix3(line, "xxx")) {
            resign();
        } else if (lower(line[0]) == 'q') {
            break;
        } else {
            printf("Commands: nav srs lrs pha tor she dam com xxx (q quits)\n");
        }
        if (!game_over) {
            check_time();
        }
    }

    if (won) {
        printf("Mission complete.\n");
    } else if (game_over) {
        printf("Mission ended with %d Klingon(s) remaining.\n", klingons_left);
    }
    return 0;
}
