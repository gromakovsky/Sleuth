#include <cstddef>

int main()
{
    size_t i = 5;
    size_t j = 10;
    size_t x = i + 2 * j;
    i += x;  // 30
    x -= 20; // 5
    auto c = new int[10];
    c[x] = 2;  // ok
    c[i] = 2;  // bad
    delete [] c;
}
