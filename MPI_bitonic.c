#include <stdlib.h>
#include <stdio.h>
#include "mpi/mpi.h"
#include <sys/time.h>
#include <time.h>

void computeActions(int*, int*, const int, const int); //calculates who to speak with for each stage
void generate(const int, int *); //used to generate random data
int cmpfunc(const void *a, const void *b); //used in quicksort
void keepHigh(int *, int *, int);
void keepLow(int *, int *, int);
void talkWith(int *, int, int);
void takeAction(int*, int, int);

int main(int argc, char *argv[]){
    int numTasks; 
    int taskID;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numTasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &taskID);

    /*
     *MASTER: check if input is correct and generates seeds
     */
    if(taskID == 0){
        if (argc != 3){
            printf("Usage: %s p q where:\n p: Collaboration network size =  2^p\n q: Each random sequence size = 2^q\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }else if ( 1<<atoi(argv[1]) != numTasks ){
            printf("Warning: run with mpirun --np %d %s %s %s for %d processes.\n",
                    1<<atoi(argv[1]), argv[0], argv[1], argv[2], 1<<atoi(argv[1]));
            MPI_Abort(MPI_COMM_WORLD, 2);
        }

    }
    //wait for master to do sanity check
    MPI_Barrier(MPI_COMM_WORLD); 
    
    int p = atoi(argv[1]);
    int q = atoi(argv[2]);
    int i = 0;
    int j = 0;

    //each process uses taskID to seed random
    srand(time(NULL)+taskID*numTasks);

    if(1<<p != numTasks && taskID==0){
        printf("Warning. numTasks is for some reason different than 2^p\n. Will continue\n");
    }
    //generate 2^p tasks to handle 2^q integers each
    numTasks = 1<<p;
    int numElements = 1<<q; 

    int *myArray;
    myArray = (int *)malloc(numElements*sizeof(int)); 
    int *partnerArray;
    partnerArray = (int *)malloc(numElements*sizeof(int));

    generate(numElements, myArray); 
    //display unsorted initial arrays
    for(i=0; i<numTasks; i++){
        if(taskID==i){
            printf("task %d array: \n", taskID);
            for(j=0; j<numElements; j++){
                printf("%d ", myArray[j]);
            }
            printf("\n");
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    qsort(myArray, numElements, sizeof(int), cmpfunc); //locally sort array
    MPI_Status status;
    for ( i=0; i<p ; i++){
        for(j=i;j>=0;j--){
            int action = ((taskID>>(i +1))%2 == 0) && ((taskID>>j)%2 == 0) || ((taskID>>(i+1))%2 != 0 && (taskID>>j)%2 != 0);
            int partner = taskID^(1<<j);
            //char *what = action ? "ascending" : "descending";
            //printf("task %d doing %s with partner %d\n", taskID, what, partner);
            if(action){

                MPI_Send(
                        myArray,
                        numElements,
                        MPI_INT,
                        partner, //destination = partner
                        i, //tag=stage
                        MPI_COMM_WORLD);
                MPI_Recv(
                        partnerArray,
                        numElements,
                        MPI_INT,
                        partner, //destination = partner
                        i, // tag = stage
                        MPI_COMM_WORLD,
                        &status);
                //sent and received, now keep low of both arrays
                keepLow(myArray, partnerArray, numElements);
            }
            else{

                MPI_Recv(
                        partnerArray,
                        numElements,
                        MPI_INT,
                        partner,
                        i,
                        MPI_COMM_WORLD,
                        &status);
                MPI_Send(
                        myArray,
                        numElements,
                        MPI_INT,
                        partner,
                        i,
                        MPI_COMM_WORLD);
                //sent and received, now keep high of both arrays
                keepHigh(myArray, partnerArray, numElements);
            }
        }
    }
    //display sorted array
    for(i=0; i<numTasks; i++){
        if(taskID==i){
            printf("task %d array: ", taskID);
            for(j=0; j<numElements; j++){
                printf("%d ", myArray[j]);
            }
            printf("\n");
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }
    free(myArray);
    free(partnerArray); //clean used space after each send/receive and manipulation

    MPI_Finalize();

    return 0;
}

void generate(const int numElements, int *myArray){
    int i=0;
    for(i=0; i<numElements; i++){
        myArray[i] = rand()%numElements;
    }
}

int cmpfunc(const void *a, const void *b){
    return ( *(int*)a - *(int*)b);
}
void keepLow(int *myArray, int *partnerArray, int numElements){
    int i=0;
    for(i=0; i < numElements; i++){
        if (partnerArray[i]<=myArray[numElements-1-i]){
            printf("%d <= %d, swapping\n", partnerArray[i], myArray[numElements-1-i]);
            myArray[numElements-1-i] = partnerArray[i];
        }
    }
    qsort(myArray, numElements, sizeof(int), cmpfunc);

}
void keepHigh(int *myArray, int *partnerArray, int numElements){
    int i=0;
    for(i=0; i < numElements; i++){
        if (partnerArray[numElements-1-i] >= myArray[i]) {
            printf("%d >= %d, swapping\n", myArray[i], partnerArray[numElements-1-i]);
            myArray[i]=partnerArray[numElements-1-i];
        }
    }

    qsort(myArray, numElements, sizeof(int), cmpfunc);
}
