/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   ConfigLoader.hpp                                   :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/06 11:42:40 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/06 11:43:51 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONFIGLOADER_HPP
#define CONFIGLOADER_HPP

#include "Config.hpp"
#include <string>

class	ConfigLoader
{
public:
	static Config				loadFromFile(const std::string &path);
	static Config				loadDefault();

};

#endif
