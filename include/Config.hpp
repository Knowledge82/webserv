/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:22:19 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/04 13:57:56 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

struct	Config
{
	std::string	host;
	int			port;

	Config();
};

class	ConfigLoader
{
public:
	static Config	loadFromFile(const std::string& path);
	static Config	loadDefault();
};

/*
 *Где расширять позже:

server { ... }, location /path { ... }
client_max_body_size
error_page 404 /errors/404.html
root, index, autoindex
cgi_pass / extensions

*/

#endif


