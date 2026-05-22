/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Autoindex.hpp                                      :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/22 11:43:50 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/22 11:44:39 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef AUTOINDEX_HPP
#define AUTOINDEX_HPP

#include <string>

namespace Http
{
	bool appendDirectoryListingHtml(std::string &outHtml,
	                                const std::string &uriWithSlash,
	                                const std::string &fsDirPath);
}

#endif
