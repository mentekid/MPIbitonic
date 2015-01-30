#include <stdlib.h>
#include <stdio.h>
#include "mpi/mpi.h"
#include <sys/time.h>
#include <time.h>

void computeActions(int*, int*, const int, const int); //calculates who to speak with for each stage
void generate(const int, const int, int *); //used to generate random data
int cmpfunc(const void *a, const void *b); //used in quicksort
void keepHigh(int *, int *, int);
void keepLow(int *, int *, int);
void talkWith(int *, int, int);
void takeAction(int*, int, int);
char *test(int *, int);
unsigned long arraySum(int*, int);

int main(int argc, char *argv[]){
    int numTasks; 
    int taskID;

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numTasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &taskID);

    /*
     *MASTER: check if input is correct 
     */
    if(taskID == 0){
        if (argc != 3){
            printf("Usage: %s p q where:\n p: Collaboration network size =  2^p\n q: Each random sequence size = 2^q\n", argv[0]);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }else if ( 1<<atoi(argv[1]) != numTasks ){
            printf("Warning: run with mpirun --np %d %s %s %s for %d processes.\n",
                    1<<atoi(argv[1]), argv[0], argv[1], argv[2], 1<<atoi(argv[1]));
            MPI_Abort(MPI_COMM_WORLD, 2);
        }else{
            printf("Running bitonic sort with %d processes and %d integers on each\n", 1<<atoi(argv[1]), 1<<atoi(argv[2]));
        }

    }
    /*
     * WORKERS: wait for master to do sanity check
     */
    MPI_Barrier(MPI_COMM_WORLD); 
    
    int p = atoi(argv[1]);
    numTasks = 1<<p;

    int q = atoi(argv[2]);
    int numElements = 1<<q; 
    
    //each process uses taskID to seed random
    srand(time(NULL)*taskID+numTasks);

    //two arrays, one for "my" (=process) data (generated at random) and one for partner's data (changes at every step)
    int *myArray;
    myArray = (int *)malloc(numElements*sizeof(int)); 
    generate(numElements, numTasks, myArray); 
    unsigned long sumStart = arraySum(myArray, numElements); //keep sum of all elements (used for parity test)
    unsigned long totalSumStart=0;
    MPI_Reduce(&sumStart, &totalSumStart, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD); //master keeps sum of all sums

    int *partnerArray;
    partnerArray = (int *)malloc(numElements*sizeof(int));

    /*
     * Sorting begins here
     */
    struct timeval startwtime, endwtime;
    MPI_Barrier(MPI_COMM_WORLD); 
    gettimeofday(&startwtime, NULL);
    qsort(myArray, numElements, sizeof(int), cmpfunc); //locally sort array
    
    MPI_Status status;
    int i=0, j=0;
    
    for ( i=0; i<p ; i++){
        for(j=i;j>=0;j--){
            int action = ((taskID>>(i +1))%2 == 0) && ((taskID>>j)%2 == 0) || ((taskID>>(i+1))%2 != 0 && (taskID>>j)%2 != 0);
            int partner = taskID^(1<<j);
            if(action){
                //TODO: how about 2 stages of send/receive for large datasets?
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
                //TODO: how about 2 stages of send/receive for large datasets?
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
    /*
     * Sorting ends here
     */
    MPI_Barrier(MPI_COMM_WORLD); 
    gettimeofday(&endwtime, NULL);
    double time = (double)((endwtime.tv_usec - startwtime.tv_usec)/1.0e6 + endwtime.tv_sec - startwtime.tv_sec);

    free(partnerArray); //not needed any more

    /*Testing that: 
     * 1. local array is sorted
     * 2. each task's smallest number (myArray[0]) is larger or equal than previous task's largest
     * 3. element sum at the beginning and end of the process is the same.
     *
     * Tests 1&2 ensure sorting was done properly
     * Test 3 ensures no number was lost during the process
     */
    unsigned long sumEnd = arraySum(myArray, numElements);
    unsigned long totalSumEnd=0;
    MPI_Reduce(&sumEnd, &totalSumEnd, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD); //master keeps sum of all sums again

    for(i=0; i<numTasks; i++){
        if(taskID==i){
            printf("task %d locality test:\t %s\n", taskID, test(myArray, numElements));
            if (taskID > 0){ //exclude first task from receiving
                int lastElm;
                MPI_Recv(&lastElm,
                        1,
                        MPI_INT,
                        taskID-1,
                        0,
                        MPI_COMM_WORLD,
                        &status);
                printf("task %d continuity test:\t %s\n", taskID, (myArray[0] >= lastElm) ? "PASS":"FAIL"); 
            }

            if ( taskID+1 != numTasks) {//exclude last task from sending
                MPI_Send(
                        &myArray[numElements-1],
                        1,
                        MPI_INT,
                        taskID+1,
                        0,
                        MPI_COMM_WORLD);
            }
            printf("\n");
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }
    free(myArray);

    //keep minimum start time and maximum end time in master
    double maxTime;
    MPI_Reduce(&time, &maxTime, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    if(taskID==0){
        printf("Master parity test:\t %s\n", (totalSumStart==totalSumEnd)?"PASS":"FAIL"); 
        printf("Sorting time: %fs\n",maxTime); 
    }
    MPI_Finalize();

    return 0;
}

// Functions

/* function generate: 
 * Stores random integers inside myArray. Uses numElements and numTasks to produce an upper limit.
 */
void generate(const int numElements, const int numTasks, int *myArray){
    int i=0;
    int lim = numElements*numTasks*10; //reduce collision chance by increasing range
    for(i=0; i<numElements; i++){
        myArray[i] = rand()%lim;
    }
}

//function cmpfunc: needed for quicksort
int cmpfunc(const void *a, const void *b){
    return ( *(int*)a - *(int*)b);
}

//scans entire array to find out-of-order items
char *test(int *myArray, int numElements) {
    int pass = 1;
    int i;
    for (i = 1; i < numElements; i++) {
        pass &= (myArray[i-1] <= myArray[i]);
    }

    return (pass) ? "PASS" : "FAIL";
}

//sums all elements in array
unsigned long arraySum(int *sumArray, int numElements){
    int i=0;
    unsigned long sum=0;
    for(i=0; i< numElements; i++){
        sum+=sumArray[i];
    }
    return sum;
}

/*
 *bitonic sorting functions keepLow, keepHigh: 
 *instead of sorting half the data ascending and the other descending, 
 *we imitate bitonic distribution by ascending one array and descending the other
 */

/*
 * function keepLow keeps the small corresponding elements from the two arrays 
 */
void keepLow(int *myArray, int *partnerArray, int numElements){
    int i=0;
    for(i=0; i < numElements; i++){
        if (partnerArray[i]<=myArray[numElements-1-i]){
            myArray[numElements-1-i] = partnerArray[i];
        }else{
            break; //since this is bitonic, if no swap was needed it means all unscanned elements in myArray are smaller than partner array
        }
    }
    qsort(myArray, numElements, sizeof(int), cmpfunc); 

}

/*
 * function keepHigh keeps the large corresponding elements from the two arrays
 */
void keepHigh(int *myArray, int *partnerArray, int numElements){
    int i=0;
    for(i=0; i < numElements; i++){
        if (partnerArray[numElements-1-i] >= myArray[i]) {
            myArray[i]=partnerArray[numElements-1-i];
        }else{
            break; //since this is bitonic, if no swap was needed it means all unscanned elements in myArray are larger than partner array
        }
    }
    qsort(myArray, numElements, sizeof(int), cmpfunc);
}
