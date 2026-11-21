#pragma once
#include <cstdio>
#include <cstdlib>

static int g_fail = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_fail; } \
    } while (0)
#define DONE()                                                   \
    do {                                                         \
        std::printf(g_fail ? "%d check(s) failed\n" : "all checks passed\n", g_fail); \
        return g_fail ? 1 : 0;                                   \
    } while (0)
