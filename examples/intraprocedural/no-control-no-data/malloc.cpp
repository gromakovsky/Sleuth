#include <stdlib.h>

int main()
{
    int * c = (int *) malloc(3 * sizeof(int));
    c[0] = 1;  // ok
    c[4] = 2;  // bad
    free(c);
}
