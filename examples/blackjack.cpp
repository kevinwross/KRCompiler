/*
 * blackjack.cpp - a text-mode blackjack game for krc.
 *
 * A fresh 52-card deck is shuffled for every hand. The player starts with a
 * $100 bankroll and a $10 bet. During betting, `+` and `-` change the wager by
 * $5 and `n` deals. During the hand, `h` hits, `d` doubles down (two cards
 * only), and `g` stands. The dealer stands on every 17, including a soft 17,
 * and hits below 17. Blackjack pays 3 to 2; a push returns the wager. There is
 * no split, insurance, or surrender.
 *
 * Money is tracked in cents so a $5 blackjack can pay $7.50. Input is one key
 * per line (blank lines are ignored), and EOF exits the game cleanly.
 *
 * Build for KR32 (from the repository root):
 *   krc -c examples/blackjack.cpp -o build/blackjack.kro
 *   krld build/blackjack.kro build/windows-msvc-debug/stage/sysroot/kr32/lib/libkrcrt.kro \
 *        targets/kr32/runtime/crt0.s targets/kr32/runtime/fileio.s \
 *        targets/kr32/runtime/eh.s -o build/blackjack.kr32
 *
 * Run:
 *   ..\kr32\build\windows-msvc\Debug\kr32.exe vm build/blackjack.kr32 \
 *       --max-steps 20000000 --input "n`ng`nq`n"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int BET_STEP = 500;       /* $5.00 */
static const int START_BANKROLL = 10000; /* $100.00 */
static const int START_BET = 1000;      /* $10.00 */
static const int MAX_HAND = 16;

static int bankroll;
static int bet;
static int deck[52];
static int deck_pos;
static int player[16];
static int dealer[16];
static int player_count;
static int dealer_count;

static const char *const rank_names[13] = {
    "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
};
static const char suit_names[4] = {'S', 'H', 'D', 'C'};

/* Reads one non-whitespace byte. Returns -1 at end of input. */
static int read_key(void) {
    int c = getchar();
    while (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        c = getchar();
    }
    return c;
}

static int rank_of(int card) {
    return card % 13;
}

static const char *rank_name(int card) {
    return rank_names[rank_of(card)];
}

static char suit_name(int card) {
    return suit_names[card / 13];
}

static int card_value(int card) {
    int rank = rank_of(card);
    if (rank == 0) {
        return 11;
    }
    if (rank >= 9) {
        return 10;
    }
    return rank + 1;
}

static void print_card(int card) {
    printf("%s%c", rank_name(card), suit_name(card));
}

static int hand_total(const int *hand, int count) {
    int total = 0;
    int aces = 0;
    for (int i = 0; i < count; ++i) {
        int rank = rank_of(hand[i]);
        if (rank == 0) {
            total += 11;
            ++aces;
        } else if (rank >= 9) {
            total += 10;
        } else {
            total += rank + 1;
        }
    }
    while (total > 21 && aces > 0) {
        total -= 10;
        --aces;
    }
    return total;
}

static int is_blackjack(const int *hand, int count) {
    return count == 2 && hand_total(hand, count) == 21;
}

static void print_money(int cents) {
    printf("$%d.%02d", cents / 100, cents % 100);
}

static void print_hand(const char *label, const int *hand, int count, int hide_hole) {
    printf("%s:", label);
    for (int i = 0; i < count; ++i) {
        printf(" [");
        if (hide_hole && i == 0) {
            printf("??");
        } else {
            print_card(hand[i]);
        }
        printf("]");
    }
    printf("\n");
}

static void shuffle(void) {
    for (int i = 0; i < 52; ++i) {
        deck[i] = i;
    }
    for (int i = 51; i > 0; --i) {
        int j = rand() % (i + 1);
        int held = deck[i];
        deck[i] = deck[j];
        deck[j] = held;
    }
    deck_pos = 0;
}

static int deal_card(void) {
    if (deck_pos >= 52) {
        shuffle();
    }
    return deck[deck_pos++];
}

static void add_card(int *hand, int *count, int card) {
    if (*count < MAX_HAND) {
        hand[*count] = card;
        ++*count;
    }
}

static void settle(void) {
    int player_total = hand_total(player, player_count);
    int dealer_total = hand_total(dealer, dealer_count);
    int player_natural = is_blackjack(player, player_count);
    int dealer_natural = is_blackjack(dealer, dealer_count);
    int delta = 0;
    const char *result;

    if (player_total > 21) {
        delta = -bet;
        result = "You bust. Dealer wins.";
    } else if (player_natural && dealer_natural) {
        delta = 0;
        result = "Both have blackjack. Push.";
    } else if (player_natural) {
        delta = (bet * 3) / 2;
        result = "Blackjack! You win 3 to 2.";
    } else if (dealer_natural) {
        delta = -bet;
        result = "Dealer has blackjack. You lose.";
    } else if (dealer_total > 21) {
        delta = bet;
        result = "Dealer busts. You win.";
    } else if (player_total > dealer_total) {
        delta = bet;
        result = "You win.";
    } else if (player_total < dealer_total) {
        delta = -bet;
        result = "Dealer wins.";
    } else {
        delta = 0;
        result = "Push.";
    }

    bankroll += delta;
    printf("%s\n", result);
    if (delta > 0) {
        printf("Won ");
        print_money(delta);
        printf(". ");
    } else if (delta < 0) {
        printf("Lost ");
        print_money(-delta);
        printf(". ");
    }
    printf("Bankroll: ");
    print_money(bankroll);
    printf("\n");
}

/* Returns 0 to quit the game, 1 to continue to the dealer. */
static int player_turn(void) {
    for (;;) {
        int total = hand_total(player, player_count);
        print_hand("Your hand", player, player_count, 0);
        printf("Total: %d\n", total);
        printf("[h]it  [d]ouble  [g]stand: ");
        int key = read_key();
        if (key < 0) {
            return 0;
        }
        if (key == 'h' || key == 'H') {
            int card = deal_card();
            add_card(player, &player_count, card);
            printf("You draw ");
            print_card(card);
            printf(".\n");
            total = hand_total(player, player_count);
            if (total > 21) {
                printf("Bust with %d.\n", total);
                return 1;
            }
            if (total == 21) {
                printf("21. You stand.\n");
                return 1;
            }
            continue;
        }
        if (key == 'd' || key == 'D') {
            if (player_count != 2) {
                printf("You can double only on the first two cards.\n");
                continue;
            }
            if (bankroll < bet * 2) {
                printf("You need enough bankroll to cover the doubled bet.\n");
                continue;
            }
            bet *= 2;
            printf("Bet doubled to ");
            print_money(bet);
            printf(".\n");
            int card = deal_card();
            add_card(player, &player_count, card);
            printf("You draw ");
            print_card(card);
            printf(".\n");
            total = hand_total(player, player_count);
            if (total > 21) {
                printf("Bust with %d.\n", total);
            } else {
                printf("Total: %d. You stand.\n", total);
            }
            return 1;
        }
        if (key == 'g' || key == 'G') {
            return 1;
        }
        printf("Use h to hit, d to double, or g to stand.\n");
    }
}

/* Returns 0 to quit the game, 1 when the hand is complete. */
static int play_hand(void) {
    shuffle();
    player_count = 0;
    dealer_count = 0;
    add_card(player, &player_count, deal_card());
    add_card(dealer, &dealer_count, deal_card());
    add_card(player, &player_count, deal_card());
    add_card(dealer, &dealer_count, deal_card());

    print_hand("Dealer shows", dealer, dealer_count, 1);

    int player_natural = is_blackjack(player, player_count);
    int dealer_natural = is_blackjack(dealer, dealer_count);
    if (player_natural || dealer_natural) {
        print_hand("Dealer", dealer, dealer_count, 0);
        settle();
        return 1;
    }

    if (!player_turn()) {
        return 0;
    }

    if (hand_total(player, player_count) > 21) {
        print_hand("Dealer", dealer, dealer_count, 0);
        settle();
        return 1;
    }

    print_hand("Dealer", dealer, dealer_count, 0);
    while (hand_total(dealer, dealer_count) < 17) {
        int card = deal_card();
        add_card(dealer, &dealer_count, card);
        printf("Dealer draws ");
        print_card(card);
        printf(".\n");
    }
    printf("Dealer total: %d\n", hand_total(dealer, dealer_count));
    settle();
    return 1;
}

/* Returns 0 to quit, 1 to deal a hand. */
static int betting_phase(void) {
    if (bankroll < BET_STEP) {
        return 0;
    }
    while (bet > bankroll && bet > BET_STEP) {
        bet -= BET_STEP;
    }

    for (;;) {
        printf("\nBankroll: ");
        print_money(bankroll);
        printf("   Bet: ");
        print_money(bet);
        printf("\n");
        printf("[+] raise $5  [-] lower $5  [n] deal  [q] quit: ");
        int key = read_key();
        if (key < 0) {
            return 0;
        }
        if (key == '+') {
            if (bet + BET_STEP <= bankroll) {
                bet += BET_STEP;
            } else {
                printf("The bet cannot exceed the bankroll.\n");
            }
            continue;
        }
        if (key == '-') {
            if (bet > BET_STEP) {
                bet -= BET_STEP;
            } else {
                printf("The minimum bet is ");
                print_money(BET_STEP);
                printf(".\n");
            }
            continue;
        }
        if (key == 'n' || key == 'N') {
            return 1;
        }
        if (key == 'q' || key == 'Q') {
            return 0;
        }
        printf("Use +, -, n, or q.\n");
    }
}

int main(void) {
    /* A fixed seed keeps a scripted hand reproducible. Change it for variety. */
    srand(1);
    bankroll = START_BANKROLL;
    bet = START_BET;

    printf("Blackjack\n");
    printf("Start with $100. Blackjack pays 3 to 2. Dealer stands on 17.\n");

    for (;;) {
        if (bankroll < BET_STEP) {
            printf("\nYou are out of money.\n");
            break;
        }
        if (!betting_phase()) {
            break;
        }
        if (!play_hand()) {
            printf("\nInput ended.\n");
            break;
        }
    }

    printf("Final bankroll: ");
    print_money(bankroll);
    printf("\nThanks for playing.\n");
    return 0;
}
