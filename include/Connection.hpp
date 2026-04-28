/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 14:02:44 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/04/28 14:05:11 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include <string>

class	Connection
{
public:
	enum	State
	{
		READING,
		WRITING,
		CLOSING
	};

	Connection();
	explicit Connection(int fd);

	int		fd() const;
	State	state() const;

	// какие события poll должен отслеживать для этого соединения
	short	wantedPollEvents() const;

	// return false if connection should be removed/closed
	bool	onReadable();
	bool	onWritable();

//old*	bool	shouldClose() const;

private:
	int			fd_;
	State		state_;
	std::string	in_;
	std::string	out_;
//old*	bool		close_;

//old*	void		buildResponseIfReady();
};

#endif
