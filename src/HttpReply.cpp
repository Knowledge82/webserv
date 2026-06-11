/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   HttpReply.cpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/21 18:34:47 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/11 18:37:19 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "HttpReply.hpp"

namespace Http
{
	HttpReply::HttpReply()
		: kind(REPLY_ERROR)
		, status(500)
		, contentType()
		, body()
		, redirectCode(0)
		, location()
		, cookieHeader()
	{
	}
}
