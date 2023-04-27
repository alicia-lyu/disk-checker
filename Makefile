CC = gcc
CFLAGS = -Wall -Wextra -Werror
DFLAGS = -g
DEPENDENCIES.C = read_ext2.c
EXEC = runscan
MAIN.C = runscan.c

restart: clean $(EXEC)
	$(EXEC) ./disk_images/image-01 output

default: $(EXEC)
	$(EXEC) ./disk_images/image-02 output

gdb: $(EXEC)
	gdb $(EXEC)

clean:
	rm -f $(EXEC)
	rm -rf output

$(EXEC): $(MAIN.C)
	$(CC) $(CFLAGS) $(DFLAGS) $(MAIN.C) $(DEPENDENCIES.C) -o $(EXEC)
