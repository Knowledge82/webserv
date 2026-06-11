/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpRequest.hpp                                    :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/02 18:10:44 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 16:13:55 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP
// парсинг входящего запроса (request parsing)

#include <string>
#include <map>
#include <cstddef>// для std::size_t

class	HttpRequest //превратить поток байтов в структурированный HTTP request
{
public:
	enum	State
	{
		HEADERS,	//“я пока не видел конец заголовков, продолжай кормить байтами”
		BODY,		//“заголовки распарсены, я знаю Content-Length, теперь жду body”
		COMPLETE,	//“запрос полностью готов, можно обрабатывать”
		ERROR		// “запрос невалидный, дальше смысла нет”
	};

	HttpRequest();
	~HttpRequest();

	// Это сердце дизайна incremental parse: consumes from 'buffer'
	// Limits: 
	// - maxHeaderBytes applies while searching for "\r\n\r\n"
	// - maxBodyBytes applies to Content-Length
	State	parse(std::string &buffer, std::size_t maxHeaderBytes, std::size_t maxBodyBytes);
	// Почему по ссылке и не const: Потому что метод потребляет байты:
	// съел заголовки → вырезал их из buffer, съел body → вырезал его из buffer
	// Возвращает текущую стадию. Connection по ней решает:
	// оставаться в READING и ждать pollin
	// или переходить в WRITING и готовить ответ
	// или отдавать 400 и закрывать

	State				getState() const;
	const std::string	&getMethod() const;
	const std::string	&getUri() const;
	const std::string	&getVersion() const;
	std::string			getHeader(const std::string &key) const;//normalize case-insensitive inside
	//почему не ссылка? если ключа нет, надо вернуть "" — это временная строка, а вернуть ссылку на временную нельзя 
	const std::map<std::string, std::string>	&getAllHeaders() const;

	const std::string	&getBody() const;
	std::size_t			getContentLength() const;

	int					getErrorStatus() const;

	void				reset();//reset to parse a new request (for keep-alive later)

	std::string			getCookieValue(const std::string &cookieName) const;

private:
	State								state_;//текущая стадия парсинга
	int									errorStatus_;
	std::string							method_;
	std::string							uri_;
	std::string							version_;
	std::map<std::string, std::string>	headers_;
	std::string							body_;
	std::size_t							contentLength_;
	bool								hasContentLength_;
	bool								hasChunked_;
	std::size_t							chunkBytesRemaining_;
	bool								waitingFinalCrlf_;

	bool							parseHeadersBlock(const std::string &headersBlock);//берёт цельный блок заголовков и разбирает по строкам
	bool							parseRequestLine(const std::string &line);//  парсит первую строку (строго 3 токена).
	bool							parseHeaderField(const std::string &line);// парсит строку Key: Value
	bool							parseChunkedBody(std::string &buffer, std::size_t maxBodyBytes);
	
	static void						trim(std::string &s); //убирает пробелы/таб перед/после (нужно для заголовков)
	static void						toLower(std::string &s);//нормализация ключей заголовков
	static bool						parseUnsignedSize(const std::string &s, std::size_t &out);//строгий разбор Content-Length
	static bool						parseChunkSizeHex(const std::string &line, std::size_t &out);
	static std::string				nextLine(const std::string &s, std::string::size_type &pos, bool &ok);//вытаскивает очередную строку по \r\n
	static std::string::size_type	findEndOfHeaders(const std::string &buffer);
	void							setError(int status);
};

#endif
