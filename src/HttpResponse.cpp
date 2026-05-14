/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:21:00 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/13 15:59:19 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpResponse.hpp" 
#include <sstream>
/* сборка исходящих ответов (response building)

Зачем вообще нужен HttpResponse как отдельный модуль?
Потому что у сервера есть две большие “оси сложности”:

I/O и неблокирующий цикл (poll, recv, send, состояния соединения) — это Server и Connection.
HTTP формат (как выглядят строки статуса, заголовки, где пустая строка, как считать длину) — это отдельная ответственность.
HttpResponse — это “фабрика строк ответа”. Он делает из твоих решений (“статус 404”, “отдай html”, “ошибка 413”) готовую байтовую строку, которую Connection просто отправляет через send(). */

namespace
{
	const char	*reasonPhrase(int status)
	{
		if (status == 200)
			return "OK";
		if (status == 400)
			return "Bad Request";
		if (status == 403)
			return "Forbidden";
		if (status == 404)
			return "Not Found";
		if (status == 405)
			return "Method Not Allowed";
		if (status == 413)
			return "Payload Too Large";
		if (status == 431)
			return "Request Header Fields Too Large";
		if (status == 500)
			return "Internal Server Error";
		return "Error";
		// опционально: 201/204/301/302 позже
	}

	// это дефолтная “страничка” ошибки, когда нет error_page файла
	std::string	errorBody(int status)
	{
		std::ostringstream	oss;

		oss << status << " " << reasonPhrase(status) << "\r\n";
		
		return oss.str();
	}

}

namespace	HttpResponse
{
	std::string	buildHelloResponse() // deprecated. тестовая функция на начальном этапе
	{
		const std::string	body = "Hello from webserv\n";

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
