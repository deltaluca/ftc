all:
	ftc main.f90
	gcc main.c -I. -I$(FTC) -o main $(FTC)/libftc.a
	echo 10 > input.dat
	./main input.dat

.PHONY: clean
clean:
	rm -f main.c main.h main
