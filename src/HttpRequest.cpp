/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/02 18:22:56 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/03 16:05:11 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpRequest.hpp"
#include <sstream>
#include <limits>

//явная инициализация в конструкторе, чтобы не затесался мусор для примитивов и остальных
HttpRequest()::HttpRequest()
	: state_(HEADERS)
	, method_()
	, uri_()
	, version_()
	, headers_()
	, body_()
	, contentLength_(0)
	, hasContentLength_(false)
{
}

HttpRequest()::~HttpRequest()
{
}

//Сбросить объект в исходное состояние, чтобы можно было парсить следующий запрос на том же соединении (keep-alive, pipelining)
void					HttpRequest::reset()
{
	state_ = HEADERS;
	method_.clear();
	uri_.clear();
	version_.clear();
	headers_.clear();
	body_.clear();//POST может быть большой; если не чистить — будешь держать память зря.
	contentLength_ = 0;
	hasContentLength_ = false;
}

HttpRequest()::State	HttpRequest::getState() const
{
	return state_;
}

const std::string		&HttpRequest::getMethod() const
{
	return method_;
}

const std::string		&HttpRequest::getUri() const
{
	return uri_;
}

const std::string		&HttpRequest::getVersion() const
{
	return version_;
}

//Дать единый доступ к заголовкам без того, чтобы внешний код думал о регистре.
std::string				HttpRequest::getHeader(const std::string &key) const
{
	std::string	lowercaseKey = key;//make copy of key
	toLower(lowercaseKey);		//to lowcase this copy
	
	std::map<std::string, std::string>::const_iterator	it = headers_.find(lowercaseKey);
	if (it == headers_.end())
		return "";
	return it->second;
}

const std::string		&HttpRequest::getBody() const
{
	return body_;
}

std::size_t				HttpRequest::getContentLength() const
{
	return contentLength_;
}

// отдельной функцией для читаемости
std::string::size_type	HttpRequest::findEndOfHeaders(const std::string &buffer)
{
	return buffer.find("\r\n\r\n");
}

HttpRequest::State		HttpRequest::parse(std::string &buffer)
{
	if (state_ == COMPLETE || state_ == ERROR)
		return state_;

	if (state_ == HEADERS)
	{
		std::string::size_type	endPos = findEndOfHeaders(buffer);
		if (endPos == std::string::npos)
			return HEADERS;

		// extract headers block (without the final "\r\n\r\n")
		std::string	headersBlock = buffer.substr(0, endPos);

		// consume headers + terminator from buffer
		buffer.erase(0, endPos + 4);

		if (!parseHeadersBlock(headersBlock))
		{
			state_ = ERROR;
			return state_;
		}

		// decide next state based on Content-Length
		if (!hasContentLength_ || contentLength_ == 0)
		{
			state_ = COMPLETE;
			return state_;
		}

		state_ = BODY;
	}
/////////////////////////////////////////////////////////// <====
	if (state_ == BODY)
	{
		if (buffer.size() < contentLength_)
			return BODY;

		body_.assign(buffer, 0, contentLength_);
		buffer.erase(0, contentLength_);
		state_ = COMPLETE;
		return state_;
	}

	return state_;
}

bool					HttpRequest::parseHeadersBlock(const std::string& headersBlock)
{

}

bool					HttpRequest::parseRequestLine(const std::string &line)
{

}

bool					HttpRequest::parseHeaderField(const std::string &line)
{

}

// убираем пробелы в начале и в конце строки (пригодится для заголовков)
void					HttpRequest::trim(std::string &s)
{
	std::string::size_type	start = s.find_first_not_of(" \t\r\n");
	if (start == std::string::npos)
	{
		s.clear();
		return;
	}
}

//Чтобы и "Host", и "HOST", и "host" работали одинаково.
std::string				HttpRequest::toLower(const std::string &s)
{

}

bool					HttpRequest::parseUnsignedSize(const std::string &s, std::size_t &out)
{

}
