all: maester

realm/maester.o: realm/maester.c realm/maester.h utils/system.h utils/utils.h config/config.h stock/stock.h terminal/terminal.h network/network.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c realm/maester.c -o realm/maester.o

envoy/envoy.o: envoy/envoy.c envoy/envoy.h envoy/envoy_worker.h realm/maester.h utils/system.h utils/utils.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c envoy/envoy.c -o envoy/envoy.o

envoy/envoy_worker.o: envoy/envoy_worker.c envoy/envoy_worker.h envoy/envoy.h utils/system.h utils/utils.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c envoy/envoy_worker.c -o envoy/envoy_worker.o

config/config.o: config/config.c config/config.h utils/system.h utils/utils.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c config/config.c -o config/config.o

stock/stock.o: stock/stock.c stock/stock.h utils/system.h utils/utils.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c stock/stock.c -o stock/stock.o

trade/trade.o: trade/trade.c trade/trade.h utils/system.h utils/utils.h config/config.h stock/stock.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c trade/trade.c -o trade/trade.o

transfer/transfer.o: transfer/transfer.c transfer/transfer.h utils/system.h utils/utils.h config/config.h stock/stock.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c transfer/transfer.c -o transfer/transfer.o

terminal/terminal.o: terminal/terminal.c terminal/terminal.h terminal/commands.h utils/system.h utils/utils.h realm/maester.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c terminal/terminal.c -o terminal/terminal.o

terminal/commands.o: terminal/commands.c terminal/commands.h utils/system.h utils/utils.h realm/maester.h config/config.h stock/stock.h trade/trade.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c terminal/commands.c -o terminal/commands.o

utils/utils.o: utils/utils.c utils/utils.h utils/system.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c utils/utils.c -o utils/utils.o

network/network.o: network/network.c network/network.h network/frame.h utils/system.h config/config.h stock/stock.h utils/utils.h transfer/transfer.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c network/network.c -o network/network.o

network/frame.o: network/frame.c network/frame.h utils/system.h
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 -c network/frame.c -o network/frame.o

maester: realm/maester.o envoy/envoy.o envoy/envoy_worker.o config/config.o stock/stock.o trade/trade.o transfer/transfer.o terminal/terminal.o terminal/commands.o utils/utils.o network/network.o network/frame.o
	gcc -Wall -Wextra -std=c99 -D_GNU_SOURCE -pthread -g -O0 realm/maester.o envoy/envoy.o envoy/envoy_worker.o config/config.o stock/stock.o trade/trade.o transfer/transfer.o terminal/terminal.o terminal/commands.o utils/utils.o network/network.o network/frame.o -o maester

valgrind: maester
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --trace-children=yes ./maester data/dragonstone/dragonstone.dat data/dragonstone/dragonstone.db

clean:
	rm -f realm/*.o envoy/*.o config/*.o stock/*.o trade/*.o transfer/*.o terminal/*.o utils/*.o network/*.o maester Maester
