#include <stdio.h>
#include <unistd.h>


int CalculateMoney(int countryId, int modifier)
{
    return countryId * modifier;
}


int main()
{
    while(1)
    {
        int money = CalculateMoney(5, 10);

        printf(
            "Money: %d\n",
            money
        );

        sleep(1);
    }

    return 0;
}