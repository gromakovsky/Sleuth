// This is a personal academic project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#include <stdio.h>
#include <stdlib.h>

void for_with_guard()
{
    int * arr = (int*) malloc(7 * sizeof(int));
    int x;
    for (x = 0; x < 10; ++x)
    {
        if (x < 7)
            arr[x] = 5;  // ok

        arr[x] = 6;  // bad
    }

    free(arr);
}

int main()
{
    for_with_guard();
    return 0;
}
