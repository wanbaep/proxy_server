CC=gcc
EXEC = proxy
OBJS = proxy_server.c
FILE1 = logfile

all:
		$(CC) -o $(EXEC) $(OBJS) -lcrypto

clean:
		rm -rf $(EXEC)
		rm -rf *.bak
		rm -rf *-
		rm -rf $(FILE1)

