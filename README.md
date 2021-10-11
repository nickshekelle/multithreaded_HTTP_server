httpserver.c is an httpserver that can handle GET, HEAD, and PUT requests.

To run, type make, then ./httpserver with args portnumber, logging(optional), number of threads(optional)

A run with all options would look like:

./httpserver 8080 -l logfile -N 4