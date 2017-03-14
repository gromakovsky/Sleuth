#include <cstdio>

int f()
{
    int x;
    scanf("%d", &x);
    return x;
}

int main()
{
    int * p = new int[8];
    int x = f();
    if (x == 3)
        p[x] = 8;  // good

    if (2 + x == 8)
        p[x] = 8;  // good

    if (x - 2 == 8)
        p[x] = 8;  // bad

    int y = 2 * x;
    if (x == 2)
        p[y] = 42;

    p[x] = 9;  // bad

    return 0;
}

