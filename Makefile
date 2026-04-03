CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread
TARGET = Maester$(EXE)

ifeq ($(OS),Windows_NT)
LDLIBS = -lws2_32
RM = del /Q
EXE = .exe
else
LDLIBS =
RM = rm -f
EXE =
endif

all: $(TARGET)

realm/maester.o: realm/maester.c realm/maester.h utils/system.h utils/utils.h config/config.h stock/stock.h terminal/terminal.h network/network.h
	$(CC) $(CFLAGS) -c realm/maester.c -o realm/maester.o

config/config.o: config/config.c config/config.h utils/system.h utils/utils.h
	$(CC) $(CFLAGS) -c config/config.c -o config/config.o

stock/stock.o: stock/stock.c stock/stock.h utils/system.h utils/utils.h
	$(CC) $(CFLAGS) -c stock/stock.c -o stock/stock.o

trade/trade.o: trade/trade.c trade/trade.h utils/system.h utils/utils.h config/config.h stock/stock.h
	$(CC) $(CFLAGS) -c trade/trade.c -o trade/trade.o

transfer/transfer.o: transfer/transfer.c transfer/transfer.h utils/system.h utils/utils.h config/config.h stock/stock.h
	$(CC) $(CFLAGS) -c transfer/transfer.c -o transfer/transfer.o

terminal/terminal.o: terminal/terminal.c terminal/terminal.h terminal/commands.h utils/system.h utils/utils.h realm/maester.h
	$(CC) $(CFLAGS) -c terminal/terminal.c -o terminal/terminal.o

terminal/commands.o: terminal/commands.c terminal/commands.h utils/system.h utils/utils.h realm/maester.h config/config.h stock/stock.h trade/trade.h
	$(CC) $(CFLAGS) -c terminal/commands.c -o terminal/commands.o

utils/utils.o: utils/utils.c utils/utils.h utils/system.h
	$(CC) $(CFLAGS) -c utils/utils.c -o utils/utils.o

network/network.o: network/network.c network/network.h network/frame.h utils/system.h config/config.h stock/stock.h utils/utils.h transfer/transfer.h
	$(CC) $(CFLAGS) -c network/network.c -o network/network.o

network/frame.o: network/frame.c network/frame.h utils/system.h
	$(CC) $(CFLAGS) -c network/frame.c -o network/frame.o

$(TARGET): realm/maester.o config/config.o stock/stock.o trade/trade.o transfer/transfer.o terminal/terminal.o terminal/commands.o utils/utils.o network/network.o network/frame.o
	$(CC) $(CFLAGS) realm/maester.o config/config.o stock/stock.o trade/trade.o transfer/transfer.o terminal/terminal.o terminal/commands.o utils/utils.o network/network.o network/frame.o -o $(TARGET) $(LDLIBS)

clean:
ifeq ($(OS),Windows_NT)
	-$(RM) realm\*.o config\*.o stock\*.o trade\*.o transfer\*.o terminal\*.o utils\*.o network\*.o Maester.exe maester.exe 2>NUL
else
	$(RM) realm/*.o config/*.o stock/*.o trade/*.o transfer/*.o terminal/*.o utils/*.o network/*.o Maester maester
endif
