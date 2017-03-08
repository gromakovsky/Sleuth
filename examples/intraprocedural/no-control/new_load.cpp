#include <cstddef>
#include <cstdio>

size_t const xs [] = { 1, 2, 5, 7, 9 };

int main()
{
    int b;
    int i = 2;
    auto c = new int[6];
    size_t j = xs[i];
    c[j] = 2;  // good
    scanf("%d", &b);
    if (b)
        i *= 2;

    j = xs[i];
    c[j] = 2;  // bad
    delete [] c;
}
