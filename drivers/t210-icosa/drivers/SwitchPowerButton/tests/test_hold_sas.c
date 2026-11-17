/* Host test for src/hold_sas.h:  cc -std=c99 -Wall -Wextra -Werror -I../src test_hold_sas.c && ./a.out */
#include <stdio.h>
#include <string.h>
#include "hold_sas.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

/* run a level sequence (one char per poll: '1' down, '0' up, 'r' up + release edge) and
 * collect actions as a string: D=power down U=power up S=sas down s=sas up */
static void run(unsigned hold, int start_held, const char *levels, char *out)
{
    struct swpwr_hold h; enum swpwr_action a[2]; int n, i; char *o = out;
    swpwr_hold_init(&h, start_held);
    for (; *levels; ++levels) {
        n = swpwr_hold_step(&h, hold, 50, *levels == '1', *levels == 'r', a);
        for (i = 0; i < n; ++i) *o++ = "-DUSs"[a[i]];
    }
    *o = 0;
}

int main(void)
{
    char o[64];
    /* legacy mode: edges reported at once */
    run(0, 0, "0011100", o);  CHECK(!strcmp(o, "DU"));
    run(0, 0, "00r00", o);    CHECK(!strcmp(o, "DU"));
    run(0, 1, "1100", o);     CHECK(!strcmp(o, "U"));        /* held at start: only the release */
    /* hold mode, 1000 ms = 20 polls: short press reported on release */
    run(1000, 0, "00111000", o);            CHECK(!strcmp(o, "DU"));
    run(1000, 0, "00r00", o);               CHECK(!strcmp(o, "DU"));
    /* 19 polls down = 950 ms: still a short press */
    run(1000, 0, "0111111111111111111100", o); CHECK(!strcmp(o, "DU"));
    /* 20 polls down: SAS at the 20th, released 2 polls later, no power action */
    run(1000, 0, "011111111111111111111111100", o); CHECK(!strcmp(o, "Ss"));
    /* released right after SAS_DOWN: SAS_UP comes with the release */
    run(1000, 0, "01111111111111111111100", o);    CHECK(!strcmp(o, "Ss"));
    /* two presses in a row, each handled alone */
    run(1000, 0, "0110011100", o);          CHECK(!strcmp(o, "DUDU"));
    /* held at start is swallowed, next press works */
    run(1000, 1, "1111001100", o);          CHECK(o[0] == 'D' && o[1] == 'U' && strlen(o) == 2);
    /* long hold while swallowed never fires SAS */
    run(1000, 1, "11111111111111111111111111111100", o); CHECK(o[0] == 0);
    /* SAS fires once per press even if held for very long */
    run(1000, 0, "0111111111111111111111111111111111111111111111100", o); CHECK(!strcmp(o, "Ss"));
    if (!fails) puts("hold_sas: all passed");
    return fails ? 1 : 0;
}
