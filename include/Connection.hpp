/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Connection.hpp                                     :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 14:02:44 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/14 11:51:52 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#ifndef CONNECTION_HPP
#define CONNECTION_HPP

#include "HttpRequest.hpp"
#include "Config.hpp"
#include <string>
#include <cstddef>

class	Connection
{
public:
	enum	State//метка состояния: "что мы сейчас ожидаем от этого fd"
	{
		READING,
		WRITING,
		CLOSING
	};

	Connection();
	explicit Connection(int fd, const Config *cfg, std::size_t serverIndex);

	int		getFd() const;
	State	getState() const;

	short	wantedPollEvents() const; // какие события poll должен отслеживать для этого соединения

	bool	onReadable();
	bool	onWritable();

private:
	int			fd_;//это clientFd, который вернул accept
	State		state_;//на каком этапе протокола находится соединение
	HttpRequest	request_;// Connection не должна гадать “сколько ещё читать?”. Она просто спрашивает у парсера состояние.
	std::string	in_;//накопленные входящие байты, прочитанные из сокета. Читаем кусками и склеиваем
	std::string	out_;//исходящий буфер ответа, который ещё не отправлен (или отправлен частично). Потому что send() не гарантирует “отправил всё”. Он может отправить только часть. Поэтому ты хранишь остаток в out_ и дожимаешь позже по POLLOUT.
	const Config	*cfg_; // доступ к конфигу (пока так)
	std::size_t	serverIndex_;

};

#endif
