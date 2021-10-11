httpserver:
	gcc httpserver.c -pthread -lm -o httpserver
clean:
	rm httpserver