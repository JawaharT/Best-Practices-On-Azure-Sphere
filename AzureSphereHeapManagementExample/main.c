#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int* number = (int*)malloc(sizeof(int)); // Can be performed for other data types too 

    // Check if malloc has succeeded 
    if (number != NULL) {
        // Succeeded, assign value and continue program as normal 
        *number = 99;
    }else {
        // Failed, exit program and log the issue
        return 0;
    }

    // Free allocated memory
    if (number != NULL) {
        free(number);
        number = NULL;
    }
    return 0;
}