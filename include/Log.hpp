/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Log.hpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:01:08 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/12 09:56:05 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef LOG_HPP
#define LOG_HPP

#include <cstdio>
#include <cstdarg>
#include <cstring>

#include "Colors.hpp"

namespace	Log
{
	//inline es para no romper ODR - One Definition Rule, para evitar multiple definition
	inline void	vprint(const char* level, const char* file, int line, const char* fmt, va_list ap)
	{
		const char	*colorOpen = "";
		const char	*colorClose = "";

		// Выбираем цвет в зависимости от уровня лога
        if (std::strcmp(level, "INFO") == 0)
		{
            colorOpen = GREEN;
            colorClose = RESET;
		}
        else if (std::strcmp(level, "ERROR") == 0)
		{
            colorOpen = RED;
            colorClose = RESET;
		}

		// Для DEBUG оба указателя остаются пустыми "", т.е. строка будет дефолтного цвета
		
		std::fprintf(stderr, "%s[%s] %s:%d: ", colorOpen, level, file, line);
		std::vfprintf(stderr, fmt, ap);
		std::fprintf(stderr, "%s\n", colorClose);
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

// do {} while (0) - Это пустой цикл, который выполняется ровно 0 раз.
// Компиляторы полностью вырезают его из бинарника при оптимизации.
#define LOG_DEBUG(...) do {} while (0)
#define LOG_INFO(...) do {} while (0)
#define LOG_ERR(...) Log::print("ERROR", __FILE__, __LINE__, __VA_ARGS__)

#endif

#endif
