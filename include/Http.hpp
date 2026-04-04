/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Http.hpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:58:16 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/04 14:00:12 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>

namespace	Http
{
	bool		hasEndOfHeaders(const std::string& buf);
	std::string	buildHelloResponse();
}

#endif
