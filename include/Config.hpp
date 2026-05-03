/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Config.hpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:22:19 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/02 15:11:47 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

//отделяем “данные” (Config) от “логики чтения/парсинга” (ConfigLoader), как норм пацаны

struct	Config//Почему struct, а не class: ты не прячешь поля, потому что это не объект поведения, а просто данные. В C++ так часто делают
{
	std::string	host;//строка с IPv4 адресом ("0.0.0.0", "127.0.0.1" и т.д.)
	int			port;//число порта

	Config();
};

class	ConfigLoader//Это “сервис” для загрузки конфига
{
//static методы — значит не надо создавать объект ConfigLoader. Ты просто зовёшь ConfigLoader::loadFromFile("mvp.conf")
public:
	static Config	loadFromFile(const std::string &path);
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


