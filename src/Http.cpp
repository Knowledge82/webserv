/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Http.cpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:21:00 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/28 14:18:56 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Http.hpp"

#include <sstream>

namespace	Http
{
	bool	hasEndOfHeaders(const std::string &buf)
	{
		return buf.find("\r\n\r\n") != std::string::npos;
	}

	std::string	buildHelloResponse()
	{
		const std::string	body = "Hello from webserv MVP\n";

		std::ostringstream	oss;
		oss << "HTTP/1.1 200 OK\r\n";
		oss << "Content-Type: text/plain\r\n";
		oss << "Content-Length: " << body.size() << "\r\n";
		oss << "Connection: close\r\n";
		oss << "\r\n";
		oss << body;

		return oss.str();
	}
}
