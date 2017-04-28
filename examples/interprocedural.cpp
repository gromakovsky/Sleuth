#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int external_function()
{
    int i = 50;
    scanf("%d", &i);
    return i;
}

// contract is that i \in [0, 6]
void f1(int i)
{
    int * arr = (int*) malloc(7 * sizeof(int));
    arr[i] = i;
    free(arr);
}

// contract is that i \in [0, 9]
void f2(int i)
{
    int * arr = (int*) malloc(10 * sizeof(int));
    arr[i] = i;
    free(arr);
}

int main()
{
    f1(5);
    f2(5);
    f1(10);
    return 0;
}

