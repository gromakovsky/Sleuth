int main()
{
    auto c = new int[1];
    c[0] = 1; // ok
    c[1] = 2; // bad
    delete [] c;
}
