Zahookuj calculate_maintenance() i spraw, żeby koszt wynosił ships * 3.
Zahookuj can_hire_fleet() i zawsze zwracaj 1.
Zahookuj add_money() i podwajaj amount.
Zahookuj calculate_damage() i zmodyfikuj argumenty pośrednio przez pola struktur.
Zahookuj daily_tick() i tylko loguj, że funkcja weszła i wyszła.

nm game | grep calculate

gdb ./game