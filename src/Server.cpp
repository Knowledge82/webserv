/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:18:22 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/06/10 14:37:00 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
#include "FdUtils.hpp"
#include "Log.hpp"

#include <poll.h>
#include <stdexcept>
#include <cstring>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>

/*
void	setNonBlocking(int fd)
{
	int	flags = ::fcntl(fd, F_GETFL, 0);
	if (flags < 0)
		throw std::runtime_error("fcntl(F_GETFL) failed");
	if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		throw std::runtime_error("fcntl(F_SETFL) failed");
}*/

//int	Server::activeCgiCount = 0;

Server::Server(const Config &cfg)
	: cfg_(cfg)
	, listenFds_()
	, connections_()
	, listenFdToServerIndex_()
	, pollFds_()
	, fdEntries_()
{
	setupListenSockets();
}

static int	createListenSocket(const std::string &host, int port)
{
	int	listenFd = ::socket(AF_INET, SOCK_STREAM, 0);//socket() просит ядро создать struct socket
													 //внутри себя и вернуть fd.
	//Важно: сам по себе socket() ещё не открывает порт и не “слушает”.
	//Он просто создаёт заготовку: тип определён, но ни адреса, ни порта нет.
	
	// AF_INET = IPv4(Address Family Internet),
	// альтернативы: AF_INET6(IPv6), AF_UNIX(локальные сокеты через файл)
	// SOCK_STREAM = потоковый сокет, Это означает TCP: надёжная доставка, порядок гарантирован,
	// данные идут потоком байт без границ сообщений. Альтернатива SOCK_DGRAM — это UDP.
	// 0 = протокол по умолчанию (для AF_INET + SOCK_STREAM это TCP). Можно явно IPPROTO_TCP
	if (listenFd < 0)
		throw std::runtime_error("socket failed");

/* Когда останавливается сервер, TCP-соединения не умирают мгновенно.
   Они уходят в состояние TIME_WAIT на ~2 минуты (это защита от старых пакетов в сети).
   В это время ядро считает порт занятым. bind() вернёт EADDRINUSE.
   SO_REUSEADDR говорит ядру: разреши переиспользовать адрес/порт даже если там ещё висят соединения
   в TIME_WAIT. Для разработки — жизненно необходимо,
   иначе будешь ждать 2 минуты после каждого перезапуска.

	SOL_SOCKET — уровень, на котором применяется опция (уровень сокета, не TCP/IP)
	SO_REUSEADDR — сама опция
	&yes — указатель на значение (int yes = 1 - включить)
	sizeof(yes) — размер значения */
	int	yes = 1;
	::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	setNonBlocking(listenFd);


/* sockaddr_in — структура для IPv4-адреса:
	cppstruct sockaddr_in
	{
		sa_family_t    sin_family;  // AF_INET
		in_port_t      sin_port;    // порт (сетевой байтовый порядок!)
		struct in_addr sin_addr;    // IP-адрес
		char           sin_zero[8]; // паддинг, выравнивание
	}; 		*/
	struct sockaddr_in	addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
/* Это один из самых важных концептов сетевого программирования — байтовый порядок.
Числа в памяти твоего x86/x64 процессора хранятся в little-endian: младший байт идёт первым.
Порт 8080 в hex = 0x1F90. В памяти x86: 90 1F.
Сеть работает в big-endian (network byte order): старший байт первым: 1F 90.
Если ты передашь порт без конвертации — ядро интерпретирует 0x1F90 как 0x901F = порт 36895.
Сервер стартует не на том порту.
htons = Host To Network Short (2 байта). Переставляет байты если нужно. */
	addr.sin_port = htons(static_cast<unsigned short>(port));
	if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)//Конвертирует строку вида "0.0.0.0" или "192.168.1.1" в бинарный 32-битный IPv4-адрес в network byte order и записывает его в addr.sin_addr. Зачем бинарный? Потому что ядро работает с числами, не со строками. IP "127.0.0.1" → 0x7F000001 1 = успех
		throw std::runtime_error("inet_pton failed for host");

	// каст к (struct sockaddr *), потому что bind принимает “универсальный” sockaddr*, а у нас специфичный sockaddr_in*
	if (::bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)//Привязывает сокет к адресу/порту
		throw std::runtime_error("bind failed");

/*Переводит сокет из состояния "просто создан" в состояние пассивного слушателя.
128 — размер backlog: максимальная длина очереди входящих соединений которые ядро накапливает
до того как ты вызовешь accept().
Если очередь переполнена — новые клиенты получают ECONNREFUSED или пакеты молча дропаются.
На современных Linux реальный backlog ограничен /proc/sys/net/core/somaxconn (обычно 128 или 4096).
Передавать большее значение — можно, ядро обрежет до лимита.*/
	if (::listen(listenFd, 128) < 0)// превращаем сокет в слушающий(пассивный).
									// backlog 128 - “сколько клиентов ядро может подержать в очереди,
									// пока ты их ещё не принял через accept”
		throw std::runtime_error("listen failed");

	return listenFd;

}

void	Server::setupListenSockets()
{
	if (cfg_.servers.empty())
		throw std::runtime_error("config has no servers");

	for (size_t si = 0; si < cfg_.servers.size(); ++si)
	{
		const ServerConfig &srv = cfg_.servers[si];

		if (srv.listens.empty())
			throw std::runtime_error("server has no listen directives");

		for (size_t li = 0; li < srv.listens.size(); ++li)
		{
			const ListenConfig &ln = srv.listens[li];

			int	fd = -1;
			try
			{
				fd = createListenSocket(ln.host, ln.port);
				listenFds_.push_back(fd);
				listenFdToServerIndex_[fd] = si;
				LOG_INFO("Listening on %s:%d (fd=%d)", ln.host.c_str(), ln.port, fd);
			}
			catch (...)
			{
				if (fd >= 0)
					::close(fd);
				throw;
			}
		}
	}
}


void	Server::buildPollFds()
{
	pollFds_.clear();
	fdEntries_.clear();

	// 1) listen fds
	for (size_t i = 0; i < listenFds_.size(); ++i)
	{
		struct pollfd	p;
		std::memset(&p, 0, sizeof(p));
		p.fd = listenFds_[i];
		p.events = POLLIN;// Для listen socket мы ждём только одного: новых подключений - POLLIN
		pollFds_.push_back(p);

		FdEntry	e;
		e.fd = p.fd;
		e.kind = FD_LISTEN;
		e.ownerClientFd = -1;
		e.ownerServerIndex = listenFdToServerIndex_[p.fd];
		fdEntries_.push_back(e);
	}

	// 2) client fds + cgi fds
	for (std::map<int, Connection>::iterator it = connections_.begin(); it != connections_.end(); ++it)
	{
		const int	clientFd = it->first;
		Connection	&c = it->second;

		// 2.1) client socket itself
		{
			struct pollfd	p;
			std::memset(&p, 0, sizeof(p));
			p.fd = clientFd;
			p.events = c.wantedPollEvents();
			pollFds_.push_back(p);

			FdEntry			e;
			e.fd = clientFd;
			e.kind = FD_CLIENT;
			e.ownerClientFd = clientFd;
			e.ownerServerIndex = 0;
			fdEntries_.push_back(e);
		}

		// 2.2) CGI stdin/stdout as separate fd entries (if active)
		// Connection have to:
		// - bool hasCgi() const;
		// - int getCgiStdinFd() const;
		// - int getCgiStdout() const;
		// - short wantedCgiPollEvents(int fd) const; (or 2 separate methods)
		if (c.hasCgi())
		{
			const int	inFd = c.getCgiStdinFd();
			if (inFd >= 0)
			{
				short	ev = c.wantedCgiStdinEvents();
				if (ev != 0)
				{
					struct pollfd	p;
					std::memset(&p, 0, sizeof(p));
					p.fd = inFd;
					p.events = ev;
					pollFds_.push_back(p);

					FdEntry			e;
					e.fd = inFd;
					e.kind = FD_CGI_STDIN;
					e.ownerClientFd = clientFd;
					e.ownerServerIndex = 0;
					fdEntries_.push_back(e);
				}
			}

			const int	outFd = c.getCgiStdoutFd();
			if (outFd >= 0)
			{
				short	ev = c.wantedCgiStdoutEvents();
				if (ev != 0)
				{
					struct pollfd	p;
					std::memset(&p, 0, sizeof(p));
					p.fd = outFd;
					p.events = ev;
					pollFds_.push_back(p);

					FdEntry			e;
					e.fd = outFd;
					e.kind = FD_CGI_STDOUT;
					e.ownerClientFd = clientFd;
					e.ownerServerIndex = 0;
					fdEntries_.push_back(e);
				}
			}
		}
	}
}

void	Server::handleListenEvent(const FdEntry &e, short revents)
{
	if (revents & POLLIN)
		acceptPendingConnections(e.fd);
}

// NEW VER
bool	Server::handleClientEvent(const FdEntry &e, short revents)
{
	std::map<int, Connection>::iterator	it = connections_.find(e.ownerClientFd);
	if (it == connections_.end())
		return false;

	Connection	&c = it->second;

	if (revents & (POLLERR | POLLHUP | POLLNVAL))
	{
		closeConnection(e.ownerClientFd);
		return true; // клиент закрыт
	}

	if ((revents & POLLIN) && c.getState() == Connection::READING)
	{
		if (!c.onReadable())
		{
			closeConnection(e.ownerClientFd);
			return true;
		}
		return false;
	}
	else if ((revents & POLLOUT) && c.getState() == Connection::WRITING)
	{
		if (!c.onWritable())
		{
			closeConnection(e.ownerClientFd);
			return true;
		}
		return false;
	}

	if (c.getState() == Connection::CLOSING)
	{
		closeConnection(e.ownerClientFd);
		return true;
	}
	return false;
}

// NEW VER
bool	Server::handleCgiEvent(const FdEntry &e, short revents)
{
	std::map<int, Connection>::iterator	it = connections_.find(e.ownerClientFd);
	if (it == connections_.end())
		return false;

	Connection	&c = it->second;

	if (!c.onCgiEvent(e.fd, revents))
	{
		closeConnection(e.ownerClientFd);
		return true;
	}

	// Если после обработки CGI он переключился в CLOSING — закрываем.
	if (c.getState() == Connection::CLOSING)
	{
		closeConnection(e.ownerClientFd);
		return true;
	}
	return false;
}

void	Server::closeConnection(int clientFd)
{
	std::map<int, Connection>::iterator it = connections_.find(clientFd);
	if (it == connections_.end())
		return;

	LOG_INFO("Closing client fd=%d", clientFd);

	// Пусть Connection сама закроет свои внутренние ресурсы:
	// close cgi fds, kill/waitpid if needed, etc.
	it->second.closeAllFdsAndKillCgiIfAny(); // <======================== TO ADD in Connection!
	
	::close(clientFd);
	connections_.erase(it); //важно удалить иначе останется зомби в таблице
}

void	Server::acceptPendingConnections(int listenFd)
{
	std::map<int, std::size_t>::const_iterator	sit = listenFdToServerIndex_.find(listenFd);
	std::size_t	serverIndex = 0;
	if (sit != listenFdToServerIndex_.end())
		serverIndex = sit->second;

	while (true)// accept в цикле потому что может быть больше одного клиента,
				// поэтому принимаем всех за один poll, чтобы не забивать очередь
	{
		struct sockaddr_in	clientAddr; //accept может вернуть не только fd, но и адрес клиента(IP/port)
										//Мы пока это не используем, но структура нужна по сигнатуре.
		socklen_t			clientAddrLen = sizeof(clientAddr);
		int					clientFd = ::accept(listenFd, (struct sockaddr *)&clientAddr, &clientAddrLen);
		//новый clientFd >= 0 — дескриптор активного соединения с клиентом.
		//Каждый вызов = один клиент из очереди
		//при ошибке -1 надо смотреть errno по-хорошему:
		//EAGAIN / EWOULDBLOCK Очередь пуста				- это нормальный выход из циклаreturn
		//EINTR Прерван сигналом							- можно повторить
		//EMFILE / ENFILEКончились файловые дескрипторы		- серьёзная ошибка
		//ECONNABORTED Клиент отвалился до accept()			- можно продолжить цикл
		if (clientFd < 0)
		{
			// Project rule: don't inspect errno after I/O.
			// Just stop accepting now; poll will wake us later again.
			// Это правило из условий webserv 42. Смысл: не различай ошибки по errno — просто
			// останови текущую операцию и доверься poll() разобраться дальше. 
			// Поэтому любой < 0 = выход, без анализа причины.
			return;
		}
		try
		{
			setNonBlocking(clientFd);
			connections_.insert(std::make_pair(clientFd, Connection(clientFd, &cfg_, serverIndex)));
			LOG_INFO("Accepted client fd=%d (listenFd=%d)", clientFd, listenFd);
		}
		catch (...)
		{
			::close(clientFd);
		}
	}
}

void	Server::run()
{
	while (true)
	{
		buildPollFds();

		if (pollFds_.empty())
			continue;

		// Сигнатура poll(): int poll(struct pollfd *fds, nfds_t nfds, int timeout);
		int	eventCount = ::poll(&pollFds_[0], pollFds_.size(), 1000);
		//poll принимает обычный C-массив pollfd*, а у нас vector,
		//поэтому передаём указатель на первый элемент
		//pollFds_.size() - сколько дескрипторов мониторим
		//1000 - timeout в мс = poll может “заснуть” максимум на 1 секунду, даже если событий нет.
		//Позже сделаем умнее: таймаут будет зависеть от ближайшего дедлайна соединений.
		
		if (eventCount <= 0)//Позже: на < 0 (error) можно логировать и аккуратно решать, что делать.
			continue;

		bool	clientClosed = false;
		for (size_t i = 0; i < pollFds_.size(); ++i)
		{
			if (pollFds_[i].revents == 0)
				continue;
		
			const FdEntry	&e = fdEntries_[i];
			short			re = pollFds_[i].revents;

			if (e.kind == FD_LISTEN)
				handleListenEvent(e, re);
			else if (e.kind == FD_CLIENT)
				clientClosed = handleClientEvent(e, re);
			else
				clientClosed = handleCgiEvent(e, re);

			// ЕСЛИ КЛИЕНТ ЗАКРЫЛСЯ — прерываем цикл! 
			// Массив fdEntries_ больше не валиден для этого шага.
			if (clientClosed)
				break;
		}
	}
}
