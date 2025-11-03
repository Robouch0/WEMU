##
## EPITECH PROJECT, 2025
## make
## File description:
## file
##

SRC	=	$(addprefix src/,	\
		my_utils.c		\
		main.c)

CC	=	gcc

OBJ	=	$(SRC:.c=.o)

CFLAGS	=	-Wall -Wextra

CPPFLAGS	=	-iquote include

NAME	=	elfanalyzer

all	:	$(NAME)

$(NAME)		:	$(OBJ)
		$(CC) -o $(NAME) $(OBJ)

clean	 :
	$(RM) *~
	$(RM) $(OBJ)

fclean	:	clean
	$(RM) $(NAME)

re	:	fclean all

.PHONY	:	all clean fclean re
