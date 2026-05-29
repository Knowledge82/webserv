/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.cpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/02 18:22:56 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/29 12:52:37 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpRequest.hpp"
#include "Log.hpp"
#include <sstream>
#include <limits>

//явная инициализация в конструкторе, чтобы не затесался мусор для примитивов и остальных ребят
HttpRequest::HttpRequest()
	: state_(HEADERS)
	, errorStatus_(400)
	, method_()
	, uri_()
	, version_()
	, headers_()
	, body_()
	, contentLength_(0)
	, hasContentLength_(false)
	, hasChunked_(false)
	, chunkBytesRemaining_(0)
	, waitingFinalCrlf_(false)
{
}

HttpRequest::~HttpRequest()
{
}

void					HttpRequest::setError(int status)
{
	state_ = ERROR;
	errorStatus_ = status;
}

//Сбросить объект в исходное состояние, чтобы можно было парсить следующий запрос на том же соединении (keep-alive, pipelining)
void					HttpRequest::reset()
{
	state_ = HEADERS;
	errorStatus_ = 400;
	method_.clear();
	uri_.clear();
	version_.clear();
	headers_.clear();
	body_.clear();//POST может быть большой; если не чистить — будешь держать память зря.
	contentLength_ = 0;
	hasContentLength_ = false;
	hasChunked_ = false;
	chunkBytesRemaining_ = 0;
	waitingFinalCrlf_ = false;
}

HttpRequest::State		HttpRequest::getState() const
{
	return state_;
}

int						HttpRequest::getErrorStatus() const
{
	return errorStatus_;
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

HttpRequest::State		HttpRequest::parse(std::string &buffer,
		std::size_t maxHeaderBytes, std::size_t maxBodyBytes)
{
	if (state_ == COMPLETE || state_ == ERROR)
		return state_;

	//Header size guard: if we still haven't found "\r\n\r\n" and buffer grows too much,
	//reject early (prevents memory abuse)
	if (state_ == HEADERS)
	{
		if (findEndOfHeaders(buffer) == std::string::npos
				&& buffer.size() > maxHeaderBytes)
		{
			setError(431);
			return state_;
		}
	}

	if (state_ == HEADERS)
	{
		std::string::size_type	termPos = findEndOfHeaders(buffer);
		if (termPos == std::string::npos)
			return HEADERS;

		// extract headers block (without the final "\r\n\r\n")
		std::string	headersBlock = buffer.substr(0, termPos + 2);

		// consume headers + terminator from buffer
		buffer.erase(0, termPos + 4);

		if (!parseHeadersBlock(headersBlock))
		{
			setError(400);
			return state_;
		}

		LOG_DEBUG("------------------ HEADERS PARSED ------------------");
		LOG_DEBUG("REQ %s %s: hasChunked=%d hasCL=%d CL=%zu TE='%s'",
			method_.c_str(), uri_.c_str(), hasChunked_ ? 1 : 0,
			hasContentLength_ ? 1 : 0, contentLength_,
			getHeader("transfer-encoding").c_str());
		LOG_DEBUG("---------------------------------------------------");
		
		// If there is a body, validate it against maxBodyBytes
		if (hasContentLength_ && contentLength_ > maxBodyBytes)
		{
			setError(413);
			return state_;
		}

		// Decide next state
		if (hasChunked_)
		{
			state_ = BODY;
		//	return state_;
		}
		else if (hasContentLength_ && contentLength_ > 0)
		{
			state_ = BODY;
		//	return state_;
		}
		else
		{
			state_ = COMPLETE;
			return state_;
		}
	}

	if (state_ == BODY)
	{
		if (hasChunked_)
		{
			if (!parseChunkedBody(buffer, maxBodyBytes))
				return BODY; // need more data
			return state_; // COMPLETE or ERROR
		}

		if (buffer.size() < contentLength_)
			return BODY;

		body_.assign(buffer, 0, contentLength_);
		buffer.erase(0, contentLength_);
		state_ = COMPLETE;
		return state_;
	}

	return state_;
}

/*
POST / HTTP/1.1
Transfer-Encoding: chunked

1a\r\n                  ← размер chunk'а в hex (1a = 26 байт)
abcdefghijklmnopqrstuvwxyz\r\n
5\r\n                   ← следующий chunk, 5 байт
hello\r\n
0\r\n                   ← последний chunk, размер 0 = конец
\r\n
*/
bool					HttpRequest::parseChunkSizeHex(const std::string &line, std::size_t &out)
{
	//Строка размера может содержать chunk extensions после точки с запятой. Нас интересует до ';'
	std::string::size_type	semi = line.find(';');
	std::string				num = (semi == std::string::npos) ? line : line.substr(0, semi);

	if (num.empty())
		return false;
	
	std::istringstream	iss(num);
	std::size_t			v = 0;

	iss >> std::hex >> v; //std::hex — это манипулятор потока - "читай следующее число как шестнадцатеричное"
	if (iss.fail())
		return false;

	//strict: no extra tokens
	char	extra;
	if (iss >> extra)
		return false;

	out = v;
	return true;
}

bool					HttpRequest::parseChunkedBody(std::string &buffer, std::size_t maxBodyBytes)
{
	while (true)
	{
		// NEW: if we already saw the final 0-chunk size line,
        // we are waiting ONLY for the terminating CRLF.
		if (waitingFinalCrlf_)
		{
			if (buffer.size() < 2)
				return false;
			
			if (buffer[0] != '\r' || buffer[1] != '\n')
			{
				setError(400);
				return true;
			}
			buffer.erase(0, 2);

			waitingFinalCrlf_ = false;
			contentLength_ = body_.size();
			hasContentLength_ = true;
			state_ = COMPLETE;
			LOG_DEBUG("CHUNKED COMPLETE: body.size()=%zu bufferRemaining=%zu",
					body_.size(), buffer.size());
			return true;
		}

		if (chunkBytesRemaining_ == 0)
		{
			// Читаем заголовок след chunk'а
			// Need "<hex>\r\n"
			std::string::size_type	eol = buffer.find("\r\n");
			if (eol == std::string::npos)
				return false; // need more data, we waiting
			
			std::string	line = buffer.substr(0, eol);
			buffer.erase(0, eol + 2);

			std::size_t	n = 0;
			if (!parseChunkSizeHex(line, n))
			{
				setError(400);
				return true;
			}

			chunkBytesRemaining_ = n;

			if (chunkBytesRemaining_ == 0) // Chunk размером 0 — это сигнал конца тела.
			{
				// Instead of trying to consume final CRLF right now (may arrive in pieces),
				// switch to "waiting for final CRLF" sub-state and loop.
				waitingFinalCrlf_ = true;
				continue;
			}
		}
		if (buffer.size() < chunkBytesRemaining_ + 2)
			return false;

		if (body_.size() + chunkBytesRemaining_ > maxBodyBytes)
		{
			setError(413);
			return true;
		}

		body_.append(buffer, 0, chunkBytesRemaining_);
		buffer.erase(0, chunkBytesRemaining_);

		if (buffer[0] != '\r' || buffer[1] != '\n')
		{
			setError(400);
			return true;
		}
		buffer.erase(0, 2);

		chunkBytesRemaining_ = 0;
	}
}

bool					HttpRequest::parseHeadersBlock(const std::string &headersBlock)
{
	bool					ok = true;
	std::string::size_type	pos = 0;

	// Request line
	std::string				requestLine = nextLine(headersBlock, pos, ok);
	if (!ok || requestLine.empty())
		return false;
	if (!parseRequestLine(requestLine))
		return false;

	// Header fields until end
	while (pos < headersBlock.size())
	{
		std::string			line = nextLine(headersBlock, pos, ok);
		if (!ok)
			return false;
		
		//Strict: empty line inside headersBlock is INVALID
		if (line.empty())
			return false;
		if (!parseHeaderField(line))
			return false;
	}

	std::string cl = getHeader("content-length");
	if (!cl.empty())
	{
		std::size_t	val;
		if (!parseUnsignedSize(cl, val))
			return false;
		contentLength_ = val;
		hasContentLength_ = true;
	}
	else
	{
		contentLength_ = 0;
		hasContentLength_ = false;
	}

	std::string te = getHeader("transfer-encoding");
	toLower(te);
	if (!te.empty() && te == "chunked")
		hasChunked_ = true;
	else
		hasChunked_ = false;
	
	if (hasChunked_ && hasContentLength_)
		return false; // 400
	return true;
}

bool					HttpRequest::parseRequestLine(const std::string &line)
{
	std::istringstream	iss(line);
	std::string			method;
	std::string			uri;
	std::string			version;

	iss >> method >> uri >> version;
	if (!iss)
		return false;

	//Strict: Extra tokens are INVALID for request line
	std::string	extraToken;
	if (iss >> extraToken)
		return false;

	if (method.empty() || uri.empty() || version.empty())
		return false;

	// Basic sanity for version
	if (version.compare(0, 5, "HTTP/") != 0)
		return false;

	method_ = method;
	uri_ = uri;
	version_ = version;

	return true;
}

bool					HttpRequest::parseHeaderField(const std::string &line)
{
	std::string::size_type	delim = line.find(':');
	if (delim == std::string::npos)
		return false;

	std::string				key = line.substr(0, delim);
	std::string				value = line.substr(delim + 1);

	trim(key);
	trim(value);

	toLower(key);

	// for MVP we keep last value if duplicate keys. AND FOR NORMAL WEBSERV?
	headers_[key] = value;

	return true;
}

// убираем пробелы в начале и в конце строки
void					HttpRequest::trim(std::string &s)
{
	const char	*ws = " \t\r\n"; //"мусорные" символы, whitespaces для HTTP header parsing

	std::string::size_type	start = s.find_first_not_of(ws);
	if (start == std::string::npos)// means string is all whitespaces
	{
		s.clear();
		return;
	}
	
	std::string::size_type	end = s.find_last_not_of(ws);
	// now from [start] to [end] - meaningful part
	s = s.substr(start, end - start + 1);// + 1 потому что end включительно
	// создание новой строки:
	// Если бы ты писал high-performance парсер как nginx — ты бы делал offsets, а не копии
}

//Чтобы и "Host", и "HOST", и "host" работали одинаково.
void					HttpRequest::toLower(std::string &s)
{
	for (std::string::size_type i = 0; i < s.size(); ++i)
	{
		char	c = s[i];
		if (c >= 'A' && c <= 'Z')
			s[i] = static_cast<char>(c - 'A' + 'a');
	}
}

bool					HttpRequest::parseUnsignedSize(const std::string &s, std::size_t &out)
{
	//Strict: entrire string must be an unsigned int
	std::istringstream	iss(s);
	unsigned long		v = 0;

	iss >> v;
	if (!iss)
		return false;

	char				extra;
	if (iss >> extra)
		return false;

	if (v > static_cast<unsigned long>(std::numeric_limits<std::size_t>::max()))
		return false;

	out = static_cast<std::size_t>(v);
	return true;
}

std::string				HttpRequest::nextLine(const std::string &s, std::string::size_type &pos, bool &ok)
{
	ok = true;
	if (pos >= s.size())
		return "";

	std::string::size_type	end = s.find("\r\n", pos);
	if (end == std::string::npos)
	{
		//Strict: header lines must end with CRLF inside headerBlock
		ok = false;
		return "";
	}

	std::string	line = s.substr(pos, end - pos);
	pos = end + 2;

	return line;
}
