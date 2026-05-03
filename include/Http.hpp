/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Http.hpp                                           :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 13:58:16 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/01 17:02:59 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef HTTP_HPP
#define HTTP_HPP

#include <string>

// вынес HTTP‑специфичную логику в отдельный модуль, чтобы Connection не занимался “строковыми правилами HTTP”
namespace	Http
{
	bool		hasEndOfHeaders(const std::string& buf);
	std::string	buildHelloResponse();
}

#endif
