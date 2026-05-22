/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpReply.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:29:56 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/21 19:07:16 by vdarsuye         ###   ########.fr       */
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

		int			status; 		// for NORMAL/ERROR
		std::string	contentType;	// for NORMAL (optional for ERROR)
		std::string	body;			// for NORMAL or custom error body

		int			redirectCode;	// for REDIRECT
		std::string	location;		// for REDIRECT
		
		HttpReply();
	};
}

#endif
