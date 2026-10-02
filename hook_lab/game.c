#include <stdio.h>
#include <unistd.h>
#include "game.h"

int calculate_maintenance(Country *country, Fleet *fleet)
{
    return fleet->ships * 2;
}

int calculate_damage(Fleet *attacker, Fleet *defender)
{
    int damage = attacker->ships * attacker->morale / 100;

    if (damage < 1)
        damage = 1;

    return damage;
}

int can_hire_fleet(Country *country, Fleet *fleet)
{
    if (country->treasury < 100)
        return 0;

    if (fleet->ships < 5)
        return 0;

    return 1;
}

void add_money(Country *country, int amount)
{
    country->treasury += amount;
}

void daily_tick(Country *country, Fleet *fleet)
{
    int maintenance = calculate_maintenance(country, fleet);

    country->treasury -= maintenance;

    printf(
        "tick | treasury=%d ships=%d maintenance=%d can_hire=%d\n",
        country->treasury,
        fleet->ships,
        maintenance,
        can_hire_fleet(country, fleet)
    );
}

int main(void)
{
    Country france = {
        .id = 1,
        .treasury = 1000,
        .ships = 0,
        .at_war = 1
    };

    Fleet venice_fleet = {
        .id = 10,
        .owner_id = 2,
        .ships = 20,
        .morale = 80
    };

    printf("Country addr: %p\n", (void *)&france);
    printf("Fleet addr:   %p\n", (void *)&venice_fleet);

    while (1)
    {
        daily_tick(&france, &venice_fleet);

        sleep(1);
    }

    return 0;
}