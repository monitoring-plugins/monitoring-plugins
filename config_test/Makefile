
all: child_test

child_test: child_test.c
	gcc -o child_test child_test.c

test:
	./run_tests 10 100 > /dev/null

clean:
	rm -f child_test

