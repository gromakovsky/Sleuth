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
        p[x] = 8;

    return 0;
}

