#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#define NUM_THREADS     5

// Global Variables

char * data;

void * PrintHello(void *threadid) {
   printf("Hello World! It's me, thread!\n");
   // while(!strcmp(data, "exit")) {

   // }
   pthread_exit(NULL);
}

int main (int argc, char *argv[]) {
   pthread_t thread;
   int rc;
   long t;
   cout << "In main: creating thread" << endl;
   rc = pthread_create(&thread, NULL, PrintHello, NULL);
   if (rc){
      printf("ERROR; return code from pthread_create() is %d\n", rc);
      exit(-1);
   }
   
   string input;
   cin >> input;
   data = input;
   while (input!="exit") {
      cin >> input;
      data = input;
   }
   cout << "Exit!!" << endl;

   /* Last thing that main() should do */
   pthread_exit(NULL);
   return 0;
}