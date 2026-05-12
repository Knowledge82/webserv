/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:21:00 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/12 17:47:04 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpResponse.hpp" 
#include <sstream>
// сборка исходящих ответов (response building)

namespace // анонимный namespace — C++ способ сказать "эти функции видны только в этом .cpp файле". 
		  // Аналог static для функций в C, но для C++
{
	const char	*reasonPhrase(int status)
	{
		if (status == 400)
			return "Bad Request";
		if (status == 413)
			return "Payload Too Large";
		if (status == 431)
			return "Request Header Fields Too Large";
		return "Error";
		// добавим потом 200/404/500
	}

	std::string	errorBody(int status)
	{
		std::ostringstream	oss;

		oss << status << " " << reasonPhrase(status) << "\n";
		
		return oss.str();
	}

}

namespace	HttpResponse
{
	std::string	buildHelloResponse()
	{
		const std::string	body = "Hello from webserv MVP\n";

		std::ostringstream	oss;
		oss << "HTTP/1.1 200 OK\r\n";						// status line
		oss << "Content-Type: text/plain\r\n";				// header
		oss << "Content-Length: " << body.size() << "\r\n"; // header
		oss << "Connection: close\r\n";						// header
		oss << "\r\n";										// empty line as delimiter headers/body
		oss << body;										// body ровно Content-Length байт

		return oss.str();
	}
	
	std::string	buildErrorResponse(int status)
	{
		const std::string	body = errorBody(status);

		std::ostringstream	oss;
		oss << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n";
		oss << "Content-Type: text/plain\r\n";
		oss << "Content-Length: " << body.size() << "\r\n";
		oss << "Connection: close\r\n";
		oss << "\r\n";
		oss << body;

		return oss.str();
	}

	std::string	buildResponse(int status, const std::string &contentType, const std::string &body)
	{

		std::ostringstream	oss;

		oss << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n";
		oss << "Content-Type: " << contentType << "\r\n";
		oss << "Content-Length: " << body.size() << "\r\n";
		oss << "Connection: close\r\n";
		oss << "\r\n";
		oss << body;

		return oss.str();

	}
}
