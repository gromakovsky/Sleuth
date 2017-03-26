#include <cstddef>

int main()
{
    int * arr = new int[10];
    size_t y = 0;
    for (size_t x = 0; x < 10; ++x)
    {
        arr[y++] = 88;
    }

    return 0;
}
