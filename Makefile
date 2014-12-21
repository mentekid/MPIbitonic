all: MPI_bitonic

MPI_bitonic: MPI_bitonic.c
	mpicc MPI_bitonic.c -O4 -o MPI_bitonic

test:
	mpirun --np 8 MPI_bitonic 3 5

clean:
	rm MPI_bitonic
