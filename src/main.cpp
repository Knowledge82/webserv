/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   main.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:21:42 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/06 12:16:20 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Config.hpp"
#include "ConfigLoader.hpp"
#include "Server.hpp"

#include <iostream>

static int	printUsage()
{
	std::cerr << "Usage:" << std::endl;
	std::cerr << "./webserv [config_file]" << std::endl;
	std::cerr << "./webserv --check-config [config_file]" << std::endl;
	
	return 1;
}

int	main(int argc, char **argv)
{
	try
	{
		Config	cfg;

		if (argc == 1)
			cfg = ConfigLoader::loadDefault();
		else if (argc == 2)
		{
			if (std::string(argv[1]) == "--check-config")
			{
				cfg = ConfigLoader::loadDefault();
				std::cout << "OK: default config" << std::endl;
				return 0;
			}
			cfg = ConfigLoader::loadFromFile(argv[1]);
		}
		else if (argc == 3)
		{
			if (std::string(argv[1]) != "--check-config")
				return printUsage();
			
			cfg = ConfigLoader::loadFromFile(argv[2]);
			std::cout << "OK: " << argv[2] << std::endl;
			return 0;
		}
		else
			return printUsage();

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
