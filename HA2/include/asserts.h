#ifndef ASSERTS_H
#define ASSERTS_H

#include <stdio.h>

#define SOFT_ASSERT(cond, err, ret)                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            fprintf(stderr, "Condition: %s failed, error: %s\n",          \
            #cond, err);                                                \
            return (ret);                                               \
        }                                                               \
    } while (0)

#endif