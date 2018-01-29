/* 
 * myread.c - A handy program for testing your tiny shell
 * 
 * usage: myread <n>
 * Read a maximum of n bytes from standard input.
 * 
 * (Total bytes read will be either the max value n, or
 * the number of bytes of input, whichever is lower.)
 *
 */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(int argc, char **argv) 
{
    int i, n;
    char c;

    if (argc != 2) {
	fprintf(stderr, "Usage: %s <n>\n", argv[0]);
	exit(0);
    }
    n = atoi(argv[1]);
    for (i=0; (c=fgetc(stdin))!=EOF &&  n>0; i++, n--)
	printf(" character read: %c \n", c);
    printf("Read %d bytes from standard input.\n", i);
    exit(0);
}
