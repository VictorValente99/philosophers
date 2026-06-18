#include "philo.h"

int	main(int ac, char **av)
{
	if (ac == 5 || ac == 6)
	{
		//correct number of arguments, we can initialize the table struct
		//1) errors checking, filling the table
		parse_input(&table, av);
		//2) initialize the mutexes and the philosophers
		data_init(&table);
		//3) start the simulation
		dinner_time(&table);
		//4) free the memory and destroy the mutexes
		//just when philos are full || 1 died
		clean(&table);
	}
	else
	{
		error_exit("Wrong input.");
		return (1);
	}
}