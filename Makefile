NAME        = ircserv

CXX         = c++
CXXFLAGS    = -Wall -Wextra -Werror -std=c++98 -pedantic
CPPFLAGS    = -I$(INC_DIR) -MMD -MP

SRC_DIR     = srcs
OBJ_DIR     = obj
INC_DIR     = includes

# 소스 파일 목록
SRCS        = $(SRC_DIR)/main.cpp \
              $(SRC_DIR)/Server.cpp \
              # $(SRC_DIR)/Client.cpp \
              # $(SRC_DIR)/Channel.cpp \
              # $(SRC_DIR)/Command.cpp

OBJS        = $(SRCS:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)
DEPS        = $(OBJS:.o=.d)

RM          = rm -rf

# 출력 색상
GREEN       = \033[0;32m
YELLOW      = \033[0;33m
RED         = \033[0;31m
RESET       = \033[0m

all: $(NAME)

$(NAME): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(NAME)
	@echo "$(GREEN)[SUCCESS] $(NAME) compiled successfully.$(RESET)"

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -c $< -o $@

clean:
	@$(RM) $(OBJ_DIR)
	@echo "$(YELLOW)[CLEAN] Object files removed.$(RESET)"

fclean: clean
	@$(RM) $(NAME)
	@echo "$(RED)[FCLEAN] Executable $(NAME) removed.$(RESET)"

re: fclean all

# 헤더 변경 자동 감지 의존성 파일 인클루드
-include $(DEPS)

.PHONY: all clean fclean re
