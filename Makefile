# **************************************************************************** #
#                                                                              #
#                                                         :::      ::::::::    #
#    Makefile                                           :+:      :+:    :+:    #
#                                                     +:+ +:+         +:+      #
#    By: vdarsuye <marvin@42.fr>                    +#+  +:+       +#+         #
#                                                 +#+#+#+#+#+   +#+            #
#    Created: 2026/04/04 12:41:09 by vdarsuye          #+#    #+#              #
#    Updated: 2026/04/04 15:10:29 by vdarsuye         ###   ########.fr        #
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
CXXFLAGS := -Wall -Wextra -Werror -std=c++98

INCLUDES := -Iinclude

SRCS := src/main.cpp \
	src/Config.cpp \
	src/Server.cpp \
	src/Connection.cpp \
	src/Http.cpp

OBJS := $(SRCS:.cpp=.o)

all: $(NAME)

debug: CXXFLAGS += -DDEBUG -g
debug: re

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f (OBJS)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all debug clean fclean re
