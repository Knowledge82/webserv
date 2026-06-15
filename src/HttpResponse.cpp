/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpResponse.cpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:21:00 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 18:29:31 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpResponse.hpp" 
#include <sstream>
/* response building

Why is HttpResponse needed as a separate module?
Because the server has two major "axes of complexity":

I/O and the non-blocking loop (poll, recv, send, connection states) — that's Server and Connection.
HTTP format (status lines, headers, empty line delimiter, content length) — that's a separate responsibility.
HttpResponse is a "response string factory". It turns your decisions ("status 404", "serve html", "error 413") into a ready byte string that Connection just sends via send(). */

namespace
{
	const char	*reasonPhrase(int status)
	{
		if (status == 200)
			return "OK";
		if (status == 201)
			return "Created";
		if (status == 204)
			return "No Content";
		if (status == 301)
			return "Moved Permanently";
		if (status == 302)
			return "Found";
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
	}

	// default error "page" when there's no error_page file
	std::string	errorBody(int status)
	{
		std::ostringstream	oss;

		oss << status << " " << reasonPhrase(status) << "\r\n";
		
		return oss.str();
	}

}

namespace	HttpResponse
{
	/*
	std::string	buildHelloResponse() // deprecated. test function from early stage
	{
		const std::string	body = "Hello from webserv\n";

		std::ostringstream	oss;
		oss << "HTTP/1.1 200 OK\r\n";						// status line
		oss << "Content-Type: text/plain\r\n";				// header
		oss << "Content-Length: " << body.size() << "\r\n"; // header
		oss << "Connection: close\r\n";						// header
		oss << "\r\n";										// empty line as delimiter headers/body
		oss << body;										// body exactly Content-Length bytes

		return oss.str();
	}
	*/

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

	std::string	buildRedirectResponse(int status, const std::string &target)
	{
		std::string			body;
		std::ostringstream	oss;

		// Small human-readable body (optional, but useful)
		body = std::string("Redirecting to ") + target + "\n";//why std::string here? how does it work?
		
		oss << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n";
		oss << "Location: " << target << "\r\n";
		oss << "Content-Type: text/plain\r\n";
		oss << "Content-Length: " << body.size() << "\r\n";
		oss << "Connection: close\r\n";
		oss << "\r\n";
		oss << body;

		return oss.str();
	}

	std::string buildResponseWithCookie(int status, const std::string &contentType,
										const std::string &body,
										const std::string &cookieHeaderValue)
	{
    	std::ostringstream oss;

		oss << "HTTP/1.1 " << status << " " << reasonPhrase(status) << "\r\n";
		oss << "Content-Type: " << contentType << "\r\n";
		oss << "Content-Length: " << body.size() << "\r\n";
		oss << "Connection: close\r\n";

		// IF COOKIE IS PASSED — INJECT IT INTO HEADERS!
		if (!cookieHeaderValue.empty())
		{
			oss << "Set-Cookie: " << cookieHeaderValue << "\r\n";
		}
    
		oss << "\r\n"; // Header/body separator
		oss << body;

		return oss.str();
	}
}
