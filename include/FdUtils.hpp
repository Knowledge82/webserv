/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   FdUtils.hpp                                        :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/06/01 17:16:55 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/01 17:16:58 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef FDUTILS_HPP
#define FDUTILS_HPP

#include <fcntl.h>
#include <stdexcept>

inline void setNonBlocking(int fd)
{
	int flags = ::fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		throw std::runtime_error("fcntl(F_GETFL) failed");
	if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl(F_SETFL) failed");
}

#endif
