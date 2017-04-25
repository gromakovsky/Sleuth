// This is a personal academic project. Dear PVS-Studio, please check it.

// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int external_function()
{
    int i = 50;
    scanf("%d", &i);
    return i;
}

void if_eq()
{
    int * p = (int*) malloc(8 * sizeof(int));
    int x = external_function();
    int y = 2 * x;
    if (x == 3)
        p[x] = 8;  // good

    if (2 + x == 8)
        p[x] = 8;  // good

    if (x - 2 == 8)
        p[x] = 8;  // bad

    if (x == 2)
        p[y] = 42;  // good

    p[x] = 9;  // bad

    free(p);
}

void if_cmp()
{
    int * p = (int*) malloc(8 * sizeof(int));
    int x = external_function();
    int y = -x;
    if (x <= 3) {
        if (x > 0)
            p[x] = 8;  // good

        if (x - 2 > 0)
            p[x] = 42;  // good

    }

    if (x < 0)
        if (x > -2)
            p[y] = 7; // good

    if (x > -2)
        p[y] = 10; // bad

    p[x] = 9;  // bad
    free(p);
}

void symbolic_and_numeric()
{
    int n = external_function();
    int * arr_n = (int*) malloc(n * sizeof(int));
    int * arr_10 = (int*) malloc(10 * sizeof(int));
    int x;

    for (x = 0; x < n; ++x)
    {
        arr_n[x] = 6;  // ok
        arr_10[x] = 6;  // bad

        if (x < 10)
        {
            arr_n[x] = 6;  // ok
            arr_10[x] = 6; // ok
        }
    }

    free(arr_n);
    free(arr_10);
}

void simple_for()
{
    int * arr = (int*) malloc(10 * sizeof(int));
    int x;
    for (x = 0; x < 10; ++x)
        arr[x] = 6;  // ok

    for (x = 0; x < 15; ++x)
        arr[x] = 6;  // bad

    free(arr);
}

void harder_for()
{
    int n = external_function();
    int * arr = (int*) malloc(n * sizeof(int));
    int x;
    for (x = 0; x < n; ++x)
        arr[x] = 6;  // ok

    for (x = 0; x < n + 5; ++x)
        arr[x] = 6;  // bad

    free(arr);
}

void for_with_ne()
{
    int * arr = (int*) malloc(10 * sizeof(int));
    int x;
    for (x = 0; x != 10; ++x)
        arr[x] = 6;  // ok

    for (x = 0; x != 15; ++x)
        arr[x] = 6;  // bad

    free(arr);
}

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

// Example presented in Li, Cifuentes, Keynes paper
char * tosunds_str(char * str)
{
    int i, j, n;
    char * buf;
    n = external_function();
    buf = (char *) malloc(n * sizeof(char));
    j = 0;
    for (i = 0; i < strlen(str); i++)
    {
        if (str[i] == 10)  // potential overflow, because size of str is unknown
            buf[j++] = '%'; // good

        buf[j++] = str[i]; // buf[j++] is bad, str[i] is potential overflow
        if (j >= n) break;
    }
    if (j + 1 >= n)
        j = n - 1;

    buf[j] = '\0';  // good
    return buf;
}

// interprocedural
void inter(int i)
{
    int * arr = (int*) malloc(7 * sizeof(int));
    arr[i] = i;
    free(arr);
}

int main()
{
    for_with_guard();
    inter(5);
    inter(10);
    return 0;
}
