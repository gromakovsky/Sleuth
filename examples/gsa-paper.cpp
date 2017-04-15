int f();
void g(int);

int main()
{
    int a= f();
    if (a > 3) goto L1;
    if (a > 5)
    {
        a = 5;
L1:
        while (a < 7)
            ++a;
    }
    else
    {
        if (a * 3 < 15)
            a *= 3;
        else
            a += 6;
    }

    g(a);
}
