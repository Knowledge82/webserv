#ifndef HTTPRESPONSE_HPP
#define HTTPRESPONSE_HPP

#include <string>

namespace	HttpResponse
{
	std::string	buildHelloResponse();
	std::string	buildErrorResponse(int status);
	std::string	buildResponse(int status, const std::string &contentType, const std::string &body);
}

#endif
