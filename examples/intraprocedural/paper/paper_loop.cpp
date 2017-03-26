// Example from the paper, only accesses within loop

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
    {
        if (str[i] == 10)  // potential overflow, because size of str is unknown
            buf[j++] = '%'; // good

        buf[j++] = str[i]; // buf[j++] is bad, str[i] is potential overflow
        if (j >= n) break;
    }

    return buf;
}
