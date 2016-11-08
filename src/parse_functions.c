#include <stdio.h>
#include <string.h>
#include "parse_functions.h"

char * lastIndexOf(char * phrase, char key) {
      char * location = NULL;
      //We don't know the length, so we walk through the string from beginning to end
      do {
            if(*phrase == key){
                  location = phrase;
            }          
      }while(*(++phrase));

      return location;
}

int stringToArray(char * theString, int max_len, char* delim, char ** theArray) {
      int arrayIndex = 0;
      //Tokenize first element, store in array     
      theArray[arrayIndex++] = strtok(theString, delim);
        
      //Tokenize the rest of the elements
      while(arrayIndex < max_len && (theArray[arrayIndex++] = strtok(NULL, delim)) != NULL)
            ;
        // for(int i=1; theArray[i-1] != NULL && i < max_len; i++)
        //     theArray[i] = strtok(NULL, delim);
      return arrayIndex-1;
}