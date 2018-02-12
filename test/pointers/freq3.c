//A program to find the number of occurrances of a character in a string.

#include <stdio.h>

int lookup(char *str, char ch)
{
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

  return occurs;
}

int main(int argc, char* argv[])
{
  char str1[1000], str2[1000], str3[1000], ch;

  printf("Enter a string: ");
  fgets(str1, 1000, stdin);
  printf("Enter a string: ");
  fgets(str2, 1000, stdin);
  printf("Enter a string: ");
  fgets(str3, 1000, stdin);
  printf("Enter a character: ");
  ch = fgetc(stdin);

  int occurs = 0;
  occurs += lookup(str1, ch);
  occurs += lookup(str2, ch);
  occurs += lookup(str3, ch);

  printf("'%c' occurs %d times in the three strings.\n", ch, occurs);
}
