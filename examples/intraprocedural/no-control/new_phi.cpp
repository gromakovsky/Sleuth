#include <cstddef>
#include <cstdio>

int main()
{
    int b;
    scanf("%d", &b);
    auto c = new int[10];
    size_t i = 2;
    size_t j = 3;
    c[i] = 5;  // ok
    if (b) {
        i = 100;
        j += 5;
    }
    c[i] = 5;  // bad
    c[j] = 8;  // ok
    delete [] c;
}
