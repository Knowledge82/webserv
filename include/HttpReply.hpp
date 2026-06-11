/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpReply.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:29:56 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 18:32:03 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTPREPLY_HPP
#define HTTPREPLY_HPP

#include <string>

namespace Http
{
	enum	ReplyKind
	{
		REPLY_NORMAL,
		REPLY_REDIRECT,
		REPLY_ERROR
	};

	struct	HttpReply
	{
		ReplyKind	kind;

		int			status;
		std::string	contentType;
		std::string	body;

		int			redirectCode;
		std::string	location;

		std::string	cookieHeader;
		
		HttpReply();
	};

	inline HttpReply	makeErrorReply(int status)
	{
		HttpReply	r;
		r.kind = REPLY_ERROR;
		r.status = status;
		return r;
	}

	inline HttpReply	makeRedirectReply(int code, const std::string &target)
	{
		HttpReply r;
		r.kind = REPLY_REDIRECT;
		r.redirectCode = code;
		r.location = target;
		return r;
	}

	inline HttpReply	makeOkReply(const std::string &type, const std::string &body)
	{
		HttpReply r;
		r.kind = REPLY_NORMAL;
		r.status = 200;
		r.contentType = type;
		r.body = body;
		return r;
	}

	inline HttpReply	makeReply(int status, const std::string &type, const std::string &body)
	{
		HttpReply r;
		r.kind = REPLY_NORMAL;
		r.status = status;
		r.contentType = type;
		r.body = body;
		return r;
	}
}

#endif
