build:
	mpicc process.c -o process -lpthread
clear:
	rm process
