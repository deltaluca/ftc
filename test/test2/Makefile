all:
	ftc routines.f90 main.f90
	gcc main.c routines.c -I. -o main
	./main

.PHONY: clean
clean:
	rm -f main.c main.h main
	rm -f routines.c routines.h
