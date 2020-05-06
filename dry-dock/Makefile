CC = clang
EXE_DRYDOCK = dry-dock
EXE_DRYDOCK_SERVER = dry-dock-server
WARNINGS = -Wall -Wextra -Werror -Wno-error=unused-parameter -Wmissing-declarations -Wmissing-variable-declarations

all: dry-dock

dry-dock: dry-dock.c utils.c
	$(CC) $^ -o $(EXE_DRYDOCK)
