#ifndef GAME_H
#define GAME_H

typedef struct {
    int id;
    int treasury;
    int ships;
    int at_war;
} Country;

typedef struct {
    int id;
    int owner_id;
    int ships;
    int morale;
} Fleet;

int calculate_maintenance(Country *country, Fleet *fleet);
int calculate_damage(Fleet *attacker, Fleet *defender);
int can_hire_fleet(Country *country, Fleet *fleet);
void add_money(Country *country, int amount);
void daily_tick(Country *country, Fleet *fleet);

#endif