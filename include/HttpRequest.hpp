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
// incoming request parsing

#include <string>
#include <map>
#include <cstddef>// for std::size_t

class	HttpRequest //converts a byte stream into a structured HTTP request
{
public:
	enum	State
	{
		HEADERS,	//have not seen end of headers yet, keep feeding bytes
		BODY,		//headers parsed, know Content-Length, waiting for body
		COMPLETE,	//request fully ready to process
		ERROR		//request invalid, no point continuing
	};

	HttpRequest();
	~HttpRequest();

	// Core of the incremental parse design: consumes from 'buffer'
	// Limits: 
	// - maxHeaderBytes applies while searching for "\r\n\r\n"
	// - maxBodyBytes applies to Content-Length
	State	parse(std::string &buffer, std::size_t maxHeaderBytes, std::size_t maxBodyBytes);
	// Why by reference and non-const: the method consumes bytes —
	// ate headers → erases them from buffer, ate body → erases from buffer
	// Returns current stage. Connection decides based on it:
	// stay in READING and wait for pollin
	// or switch to WRITING and prepare response
	// or return 400 and close

	State				getState() const;
	const std::string	&getMethod() const;
	const std::string	&getUri() const;
	const std::string	&getVersion() const;
	std::string			getHeader(const std::string &key) const;//normalize case-insensitive inside
	//why not a reference? if key missing, need to return "" — that's a temp string, can't return ref to temp
	const std::map<std::string, std::string>	&getAllHeaders() const;

	const std::string	&getBody() const;
	std::size_t			getContentLength() const;

	int					getErrorStatus() const;

	void				reset();//reset to parse a new request (for keep-alive later)

	std::string			getCookieValue(const std::string &cookieName) const;

private:
	State								state_;//current parsing stage
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

	bool							parseHeadersBlock(const std::string &headersBlock);//takes a complete header block and splits into lines
	bool							parseRequestLine(const std::string &line);//parses first line (strictly 3 tokens)
	bool							parseHeaderField(const std::string &line);//parses Key: Value line
	bool							parseChunkedBody(std::string &buffer, std::size_t maxBodyBytes);
	
	static void						trim(std::string &s); //removes leading/trailing whitespace (needed for headers)
	static void						toLower(std::string &s);//normalize header keys to lowercase
	static bool						parseUnsignedSize(const std::string &s, std::size_t &out);//strict Content-Length parsing
	static bool						parseChunkSizeHex(const std::string &line, std::size_t &out);
	static std::string				nextLine(const std::string &s, std::string::size_type &pos, bool &ok);//extracts next line by \r\n
	static std::string::size_type	findEndOfHeaders(const std::string &buffer);
	void							setError(int status);
};

#endif
