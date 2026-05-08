# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/04/04 12:41:09 by vdarsuye          #+#    #+#              #
#    Updated: 2026/05/08 14:10:07 by vdarsuye         ###   ########.fr        #
#                                                                              #
# **************************************************************************** #

# Colors
RESET := \033[0m
RED := \033[31m
GREEN := \033[32m
YELLOW := \033[33m
BLUE := \033[34m
MAGENTA := \033[35m
CYAN := \033[36m
NEON_GREEN := \033[92m


NAME := webserv

CXX := c++
CXXFLAGS := -Wall -Wextra -Werror -g3 -fsanitize=address -std=c++98

INCLUDES := -Iinclude

SRCS := src/main.cpp \
	src/Config.cpp \
	src/ConfigLoader.cpp \
	src/ConfigTokenizer.cpp \
	src/ConfigParser.cpp \
	src/Server.cpp \
	src/Connection.cpp \
	src/HttpResponse.cpp \
	src/HttpRequest.cpp

OBJS := $(SRCS:.cpp=.o)

all: cls $(NAME) banner

cls:
	-clear 2>/dev/null || true

debug: CXXFLAGS += -DDEBUG -g
debug: re

banner:
	@echo "$(NEON_GREEN)"
	@cat x.txt
	@echo "$(RESET)"

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	@rm -f $(OBJS)
	@echo "$(GREEN)Object files removed.$(RESET)"

fclean: clean
	@rm -f $(NAME)
	@echo "$(GREEN)Executable removed.$(RESET)"

re: fclean all

.PHONY: all banner cls debug clean fclean re
