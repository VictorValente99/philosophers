/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   parser.c                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: victde-s <victde-s@student.42.fr>          +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/18 14:31:26 by victde-s          #+#    #+#             */
/*   Updated: 2026/06/18 16:40:42 by victde-s         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "philo.h"

static inline bool	is_space(char c)
{
	return ((c >= 9 && c <= 13) || c == 32);
}

static inline bool	is_digit(char c)
{
	return (c >= '0' && c <= '9');
}

static const char	*valid_input(const char *str)
{
	const char	*number;
	int			len;

	while (is_space(*str))
		str++;
	if (*str == '+')
		++str;
	if (*str == '-')
		error_exit("Wrong input, only feed me with some positive meal.");
	if (!is_digit(*str))
		error_exit("Wrong input, is not a correct digit.");
	number = str;
	while (is_digit(*str++))
		++len;
	if (len > 10)
		error_exit("Wrong input, number is too big.");
	return (number);
}

static long	ft_atol(const char *str)
{
	long	result;

	result = 0;
	str = valid_input(str);
	while (is_digit(*str))
		result = (result * 10) + (*str++ - '0');
	if (result > INT_MAX)
		error_exit("Wrong input, number is too big, INT_MAX is the limit.");
	return (result);
}

void	parse_input(t_table *table, char **av)
{
	table->philos_nbr = ft_atol(av[1]);
	table->time_to_die = ft_atol(av[2]) * 1e3;
	table->time_to_eat = ft_atol(av[3]) * 1e3;
	table->time_to_sleep = ft_atol(av[4]) * 1e3;
	if (table-> time_to_die < 6e4
		|| table->time_to_eat < 6e4
		|| table->time_to_sleep < 6e4)
		error_exit("Wrong input, use timestamps > than 60ms");
	if (av[5])
		table->nbr_limit_meals = atol(av[5]);
	else
		table->nbr_limit_meals = -1;
	if (table->philos_nbr <= 0 || table->time_to_die <= 0
		|| table->time_to_eat <= 0 || table->time_to_sleep <= 0
		|| (av[5] && table->nbr_limit_meals <= 0))
		error_exit("Wrong input.");
}
