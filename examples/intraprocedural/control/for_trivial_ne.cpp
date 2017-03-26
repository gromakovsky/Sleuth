#include <cstddef>

int main()
{
    int * arr = new int[10];
    for (size_t x = 0; x != 10; ++x)
        arr[x] = 5;

    return 0;
}
