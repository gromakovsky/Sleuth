// Example from the paper (slightly modified in irrelevant places)

#include <cstring>
#include <cstdlib>

int external_function()
{
    int n;
    return n;
}

char * tosunds_str(char * str)
{
    int i, j, n;
    char * buf;
    n = external_function();
    buf = (char *) malloc(n * sizeof(char));
    j = 0;
    for (i = 0; i < strlen(str); i++)
    {
        if (str[i] == 10)
            buf[j++] = '%';

        buf[j++] = str[i];
        if (j >= n) break;
    }
    if (j + 1 >= n)
        j = n - 1;

    buf[j] = '\0';
    return buf;
}
