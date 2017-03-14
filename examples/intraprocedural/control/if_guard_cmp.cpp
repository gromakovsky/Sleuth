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
    if (x <= 3)
        if (x > 0)
            p[x] = 8;  // good

    p[x] = 9;  // bad

    return 0;
}

