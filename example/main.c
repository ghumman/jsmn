#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include "../jsmn.h"




int main() {
	/*
	int a = 45;
	int b = 45;
	int c = sum(a,b);
	printf("Value of c: %d", c);
	return 0;
	*/

	char *str = "{ \"name\": \"joe\" }";
	int c = json_test(str);
	printf("Value of c: %d", c);
	return 0;
}
