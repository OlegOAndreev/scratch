CFLAGS=-O3 -Wall -Wextra -std=c++11
RSYNC_HOST=gigaminer-ng-dev.i-folb.fog.yandex.net
RSYNC_DIR=memcpy-test
RSYNC_PROD_HOST=gigaminer.metrics-srv06h.search.yandex.net

memcpy-test: main.cpp memcpy-impl.cpp
	g++ $(CFLAGS) -o $@ $^

rsync: main.cpp memcpy-impl.cpp
	rsync -zavr --exclude memcpy-test --exclude \*.dSYM --exclude \*.dtps * $(RSYNC_HOST):$(RSYNC_DIR)/
	ssh $(RSYNC_HOST) "cd $(RSYNC_DIR)/ && ~/arcadia/devtools/ya/ya tool c++ $(CFLAGS) -lrt -lstdc++ -o memcpy-test $^"
	scp $(RSYNC_HOST):$(RSYNC_DIR)/memcpy-test memcpy-test-linux
	scp memcpy-test-linux $(RSYNC_PROD_HOST):
	rm memcpy-test-linux
