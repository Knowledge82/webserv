NAME = webserv
CPPFLAGS = -Wall -Wextra -Werror -std=c++98
SRC = learning/main.cpp

all: $(NAME)

$(NAME):
	c++ $(CPPFLAGS) $(SRC) -o $@


clean:
	rm $(NAME)

fclean: clean

re: fclean all

.PHONY: clean all re fclean
