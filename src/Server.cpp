/* ************************************************************************** */
/*                                                                            */
/*                                                        :::      ::::::::   */
/*   Server.cpp                                         :+:      :+:    :+:   */
/*                                                    +:+ +:+         +:+     */
/*   By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+        */
/*                                                +#+#+#+#+#+   +#+           */
/*   Created: 2026/04/04 15:18:22 by vdarsuye          #+#    #+#             */
/*   Updated: 2026/05/08 12:55:02 by vdarsuye         ###   ########.fr       */
/*                                                                            */
/* ************************************************************************** */

#include "Server.hpp"
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
static std::runtime_error	serverError(const std::string &message)
{
	return std::runtime_error(message);
}
*/
static void	setNonBlocking(int fd)
{
	// fcntl - системная функция “управление параметрами fd”
	int	flags = ::fcntl(fd, F_GETFL, 0); // F_GETFL означает: “дай текущие file status flags этого fd”
	if (flags < 0)
		throw std::runtime_error("fcntl(F_GETFL) failed");
	if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)//старые флаги плюс дописываем бит неблокирующего режима
		throw std::runtime_error("fcntl(F_SETFL) failed");
/*
 *Что реально меняется в поведении fd после O_NONBLOCK:

accept() на listen fd не будет ждать клиента: если клиентов нет — вернёт ошибку.
recv() на client fd не будет ждать данные: если данных нет — вернёт ошибку.
send() на client fd не будет ждать, пока освободится буфер: если некуда писать — вернёт ошибку.
Но! Мы делаем правильно: перед accept/recv/send мы спрашиваем poll(), поэтому почти всегда будет “готово”, и ошибки будут редкими.
 */
}

Server::Server(const Config &cfg)
	: cfg_(cfg)
	, listenFds_()
{
	setupListenSockets();
}

static int	createListenSocket(const std::string &host, int port)
{
	int	listenFd = ::socket(AF_INET, SOCK_STREAM, 0);//socket() просит ядро создать struct socket внутри себя и вернуть fd. Как розетка:
	// есть розетка в стене (сокет в ядре),
	// у тебя есть штекер/доступ к ней (fd),
	// дальше ты подключаешься к сети, читаешь/пишешь.
	//Важно: сам по себе socket() ещё не открывает порт и не “слушает”. Он просто создаёт заготовку: тип определён, но ни адреса, ни порта нет.
	// AF_INET = IPv4(Address Family Internet), альтернативы: AF_INET6(IPv6), AF_UNIX(локальные сокеты через файл)
	// SOCK_STREAM = потоковый сокет, Это означает TCP: надёжная доставка, порядок гарантирован, данные идут потоком байт без границ сообщений. Альтернатива SOCK_DGRAM — это UDP.
	// 0 = протокол по умолчанию (для AF_INET + SOCK_STREAM это TCP). Можно явно IPPROTO_TCP
	if (listenFd < 0)
		throw std::runtime_error("socket failed");


/*
 * Когда ты останавливаешь сервер, TCP-соединения не умирают мгновенно. Они уходят в состояние TIME_WAIT на ~2 минуты (это защита от старых пакетов в сети). В это время ядро считает порт занятым. bind() вернёт EADDRINUSE.
SO_REUSEADDR говорит ядру: разреши переиспользовать адрес/порт даже если там ещё висят соединения в TIME_WAIT. Для разработки — жизненно необходимо, иначе будешь ждать 2 минуты после каждого перезапуска.

SOL_SOCKET — уровень, на котором применяется опция (уровень сокета, не TCP/IP)
SO_REUSEADDR — сама опция
&yes — указатель на значение (int yes = 1 = включить)
sizeof(yes) — размер значения
*/
	int	yes = 1;
	::setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

	setNonBlocking(listenFd);


/* sockaddr_in — структура для IPv4-адреса:
	cppstruct sockaddr_in {
		sa_family_t    sin_family;  // AF_INET
		in_port_t      sin_port;    // порт (сетевой байтовый порядок!)
		struct in_addr sin_addr;    // IP-адрес
		char           sin_zero[8]; // паддинг, выравнивание
}; */
	struct sockaddr_in	addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
/*
 *Это один из самых важных концептов сетевого программирования — байтовый порядок.
Числа в памяти твоего x86/x64 процессора хранятся в little-endian: младший байт идёт первым. Порт 8080 в hex = 0x1F90. В памяти x86: 90 1F.
Сеть работает в big-endian (network byte order): старший байт первым: 1F 90.
Если ты передашь порт без конвертации — ядро интерпретирует 0x1F90 как 0x901F = порт 36895. Сервер стартует не на том порту.
htons = Host To Network Short (2 байта). Переставляет байты если нужно.
 */
	addr.sin_port = htons(static_cast<unsigned short>(port));
	if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1)//Конвертирует строку вида "0.0.0.0" или "192.168.1.1" в бинарный 32-битный IPv4-адрес в network byte order и записывает его в addr.sin_addr. Зачем бинарный? Потому что ядро работает с числами, не со строками. IP "127.0.0.1" → 0x7F000001 1 = успех
		throw std::runtime_error("inet_pton failed for host");

	// каст к (struct sockaddr *), потому что bind принимает “универсальный” sockaddr*, а у нас специфичный sockaddr_in*
	if (::bind(listenFd, (struct sockaddr *)&addr, sizeof(addr)) < 0)//Привязывает сокет к адресу/порту
		throw std::runtime_error("bind failed");

		/*
		 *
		 * Переводит сокет из состояния "просто создан" в состояние пассивного слушателя.
128 — размер backlog: максимальная длина очереди входящих соединений которые ядро накапливает до того как ты вызовешь accept(). Если очередь переполнена — новые клиенты получают ECONNREFUSED или пакеты молча дропаются.
На современных Linux реальный backlog ограничен /proc/sys/net/core/somaxconn (обычно 128 или 4096). Передавать большее значение — можно, ядро обрежет до лимита.
	*/
	if (::listen(listenFd, 128) < 0)// превращаем сокет в слушающий(пассивный). backlog 128 - “сколько клиентов ядро может подержать в очереди, пока ты их ещё не принял через accept”
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

	for (size_t i = 0; i < listenFds_.size(); ++i)
	{
		struct pollfd	p;
		std::memset(&p, 0, sizeof(p));
		p.fd = listenFds_[i];
		p.events = POLLIN;// Для listen socket мы ждём только одного: новых подключений - POLLIN
		p.revents = 0;
		pollFds_.push_back(p);
	}

	for (std::map<int, Connection>::iterator it = connections_.begin(); it != connections_.end(); ++it)
	{
		struct pollfd	c;
		std::memset(&c, 0, sizeof(c));
		c.fd = it->first;
		c.events = it->second.wantedPollEvents();// Это разделение ответственности: Server управляет “оркестром fd”, Connection управляет “логикой протокола”, т.е. Server решает “когда ждать”, а Connection решает “чего ждать” (читать/писать)
		pollFds_.push_back(c);
	}
}

void	Server::closeConnection(int fd)
{
	std::map<int, Connection>::iterator it = connections_.find(fd);
	if (it != connections_.end())
	{
		LOG_INFO("Closing fd=%d", fd);
		::close(fd);
		connections_.erase(it);//важно удалить иначе останется зомби в таблице
	}
}

void	Server::acceptPendingConnections(int listenFd)
{
	while (true)// accept в цикле потому что может быть больше одного клиента, поэтому принимаем всех за один poll, чтобы не забивать очередь
	{
		struct sockaddr_in	clientAddr; //accept может вернуть не только fd, но и адрес клиента (IP/port). Мы пока это не используем, но структура нужна по сигнатуре.
		socklen_t			clientAddrLen = sizeof(clientAddr);
		int					clientFd = ::accept(listenFd, (struct sockaddr *)&clientAddr, &clientAddrLen);
		//новый clientFd >= 0 — дескриптор активного соединения с клиентом. Каждый вызов = один клиент из очереди
		//при ошибке -1 надо смотреть errno по-хорошему:
		//EAGAIN / EWOULDBLOCK Очередь пуста				- это нормальный выход из циклаreturn
		//EINTR Прерван сигналом							- можно повторить
		//EMFILE / ENFILEКончились файловые дескрипторы		- серьёзная ошибка
		//ECONNABORTED Клиент отвалился до accept()			- можно продолжить цикл
		if (clientFd < 0)
		{
			// Project rule: don't inspect errno after I/O.
			// Just stop accepting now; poll will wake us later again.
			// Это правило из условий webserv 42. Смысл: не различай ошибки по errno — просто останови текущую операцию и доверься poll() разобраться дальше. 
			// Поэтому любой < 0 = выход, без анализа причины.
			return;
		}
		try
		{
			setNonBlocking(clientFd);
			connections_.insert(std::make_pair(clientFd, Connection(clientFd)));
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

		// Сигнатура poll(): int poll(struct pollfd *fds, nfds_t nfds, int timeout);
		int	eventCount = ::poll(&pollFds_[0], pollFds_.size(), 1000);
		//poll принимает обычный C-массив pollfd*, а у нас vector, поэтому передаём указатель на первый элемент
		//pollFds_.size() - сколько дескрипторов мониторим
		//1000 - timeout в мс = poll может “заснуть” максимум на 1 секунду, даже если событий нет.Позже сделаем умнее: таймаут будет зависеть от ближайшего дедлайна соединений.
		if (eventCount <= 0)//Позже: на < 0 (error) можно логировать и аккуратно решать, что делать.
			continue;

		//1) Accept on all listening sockets that have POLLIN
		for (size_t i = 0; i < listenFds_.size(); ++i)
		{
			if (pollFds_[i].revents & POLLIN)//POLLIN в revents для listen socket: в очереди есть новые входящие соединения
				acceptPendingConnections(pollFds_[i].fd);
		}

		//2) Client fds start after listen fds
		for (size_t i = listenFds_.size(); i < pollFds_.size(); ++i)
		{
			int		fd = pollFds_[i].fd; // дескриптор клиента
			short	re = pollFds_[i].revents; // набор флагов событий

			std::map<int, Connection>::iterator	it = connections_.find(fd);
			if (it == connections_.end())
				continue;

			if (re & (POLLERR | POLLHUP | POLLNVAL))//клиент умер/сломался/невалиден
			{
				closeConnection(fd);
				continue;
			}

			Connection	&c = it->second;

			if ((re & POLLIN) && c.getState() == Connection::READING)//re & POLLIN - есть данные для чтения
			{
				if (!c.onReadable())
				{
					closeConnection(fd);
					continue;
				}
			}
			if ((re & POLLOUT) && c.getState() == Connection::WRITING)
			{
				if (!c.onWritable())
				{
					closeConnection(fd);
					continue;
				}
			}

			if (c.getState() == Connection::CLOSING)
				closeConnection(fd);
		}
	}
}

/*Сейчас у тебя бесконечный цикл, но при аварийном исключении/выходе хорошо бы чистить. На 42 это не всегда требуют, но “не падать” любят. Потом добавим деструктор Server::~Server() и закроем listenFds_ и все connections_.*/
