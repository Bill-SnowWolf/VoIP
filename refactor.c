#include <stdio.h>
#include <string.h>
#include <stdlib.h>
int main () {
   static const char filename[] = "tmp.c";
   FILE *fin = fopen (filename, "r");
   FILE *fout = fopen ("capture.c", "w");
   if (fin != NULL) {
      char line [256]; /* or other suitable maximum line size */
      while (fgets(line, sizeof line, fin) != NULL) {
         int i=0;
         for (i=0;i<strlen(line);i++) {
            // printf("%c:%d\n", line[i], line[i]);
            if ((line[i] < 48 || line[i] > 57) && line[i] != 32) {
               printf("%d\n", i);
               break;
            }
         }

         char *newLine = (char *)malloc(strlen(line) - i + 1);
         strncpy(newLine, line + i, strlen(line) - i);

         fputs (newLine, fout ); /* write the line */
      }
      fclose(fin);
      fclose(fout);
   }
   else
   {
      perror ( filename ); /* why didn't the file open? */
   }
   return 0;
}