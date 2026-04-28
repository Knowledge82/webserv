/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:21:42 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/28 11:35:52 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"
#include "Server.hpp"

#include <iostream>

int	main(int argc, char **argv)
{
	try
	{
		Config	cfg;

		if (argc == 1)
			cfg = ConfigLoader::loadDefault();
		else if (argc == 2)
			cfg = ConfigLoader::loadFromFile(argv[1]);
		else
		{
			std::cerr << "Usage: ./webserv [config_file]" << std::endl;
			return 1;
		}

		Server	s(cfg);
		s.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Fatal: " << e.what() << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cerr << "Fatal: unknown error" << std::endl;
		return 1;
	}

	return 0;
}
