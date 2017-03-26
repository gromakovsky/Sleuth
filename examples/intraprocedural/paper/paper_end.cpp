// Example from the paper, last memory access

#include <cstring>
#include <cstdlib>

int external_function();

char * tosunds_str(char * str)
{
    int i, j, n;
    char * buf;
    n = external_function();
    buf = (char *) malloc(n * sizeof(char));
    j = 0;
    for (i = 0; i < strlen(str); i++)
        j++;

    if (j + 1 >= n)
        j = n - 1;

    buf[j] = '\0';  // good
    return buf;
}
