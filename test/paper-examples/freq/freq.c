//A program to find the number of occurrances of a character in a string.

#include <stdio.h>

int main(int argc, char* argv[])
{
  char str[1000], ch;

  printf("Enter a string: ");
  fgets(str, 1000, stdin);
  printf("Enter a character: ");
  ch = fgetc(stdin);

  char* str_ptr = str;
  int occurs = 0;
  while(*str_ptr != '\0')
  {
    if(*str_ptr == ch)
    {
      occurs++;
    }
    str_ptr++;
  }

  printf("'%c' occurs %d times in the string.\n", ch, occurs);
}
