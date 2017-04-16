#include <cstddef>

int main()
{
    int * arr = new int[7];
    for (int x = 0; x < 10; ++x)
    {
        if (x < 7)
            arr[x] = 5;  // ok

        arr[x] = 6;  // bad
    }

    return 0;
}
