SRC	=	$(addprefix src/,	\
		main.cpp)

CC	=	g++

OBJ	=	$(SRC:.cpp=.o)

CFLAGS = -std=c++17 -I. -I$(VULKAN_SDK_PATH)/include

LDLIBS = -L$(VULKAN_SDK_PATH)/lib `pkg-config --static --libs glfw3` -lvulkan

NAME = WEMU

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) -o $(NAME) $(OBJ) $(LDLIBS)

clean:
	$(RM) $(OBJ)

fclean: clean
	$(RM) $(NAME)

re:	fclean all

test: a.out
	./a.out

.PHONY: test clean