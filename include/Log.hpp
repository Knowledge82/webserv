/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Log.hpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:01:08 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/04 15:40:58 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef LOG_HPP
#define LOG_HPP

#include <cstdio>
#include <cstdarg>

namespace	Log
{//inline mas abajo es para no romper ODR - One Definition Rule
	inline void	vprint(const char* level, const char* file, int line, const char* fmt, va_list ap)
	{
		std::fprintf(stderr, "[%s] %s:%d: ", level, file, line);
		std::vprintf(stderr, fmt, ap);
		std::fprintf(stderr, "\n");
	}

	inline void	print(const char* level, const char* file, int line, const char* fmt, ...)
	{
		va_list	ap;
		va_start(ap, fmt);
		vprint(level, file, line, fmt, ap);
		va_end(ap);
	}
}

#ifdef DEBUG

#define LOG_DEBUG(...) Log::print("DEBUG", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...) Log::print("INFO", __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERR(...) Log::print("ERROR", __FILE__, __LINE__, __VA_ARGS__)

#else

#define LOG_DEBUG(...) do {} while (0)
#define LOG_INFO(...) do {} while (0)
#define LOG_ERR(...) do {} while (0)

#endif

#endif
