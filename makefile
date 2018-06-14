tm-sim: tm-sim.c
	gcc -g -std=c11 -Wall -static -O2 -o tm-sim tm-sim.c
	# gcc -DEVAL -std=c11 -O2 -pipe -static -s -o tm-sim tm-sim.c
