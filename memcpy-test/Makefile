CFLAGS=-O3 -Wall -Wextra -std=c++11

memcpy-test: main.cpp memcpy-test.cpp
	g++ -O3 -Wall -Wextra -std=c++11 -o $@ $^

rsync: main.cpp memcpy-test.cpp
	rsync -zavr --exclude memcpy-test --exclude \*.dSYM --exclude \*.dtps * gigaminer-ng-dev.i-folb.fog.yandex.net:memcpy-test/
	ssh gigaminer-ng-dev.i-folb.fog.yandex.net "cd memcpy-test/ && ~/arcadia/devtools/ya/ya tool c++ $(CFLAGS) -lrt -lstdc++ -o memcpy-test $^"
