/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Cgi.hpp                                            :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/05/25 15:03:48 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/25 15:06:09 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CGI_HPP
#define CGI_HPP

namespace Http
{
	bool	runCgi(std::string &outCgiStdout,
					int &outStatus,					// 500 on exec/fork/pie error
					const std::string &cgiPath,
					const HttpRequest &req,
					const std::string &scriptFsPath);
}

#endif
