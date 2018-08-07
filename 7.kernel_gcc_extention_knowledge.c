#include <stdio.h>
#include <stdlib.h>

typedef struct{
	int size;
	char t;
}ngate, *pngate;

typedef struct mproject{
	char* name;
	int length;
	int flag;
}project;

#define offsetof(TYPE, MEMBER) ((size_t)&((TYPE *)0)->MEMBER)


#define container_of(ptr, type, member) ({	    \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
	(type *)( (char *)__mptr - offsetof(type,member) );})


int main()
{
	int a;
	printf("this is extension knowledge\n");
	printf("===== using typeof ========\n");
	//typeof(&a) b;
	//*b = 10;
	//printf("a is int, typeof(&a) b is *int\n");
	//printf("b is %d\n",*b);
	typeof(((ngate *)0)->t) w;
	w = 'u';
	printf("w is a char %c\n",w);
	typeof(((ngate *)15)->t) lu;
	w = 'n';
	printf("lu also is a char type and I can use any const value,now I use 15 replacing 0, lu is %c\n",w);

	printf("using offsetof %d\n",offsetof(ngate,t));
	printf("for project struct, flag offset %ld\n",offsetof(project,flag));
	printf("int %ld, char* %ld\n",sizeof(int),sizeof(char*));

	project *myproject = malloc(sizeof(project));
	myproject->name="myproject";
	myproject->length = 10;
	myproject->flag = 1;
	printf("myproject pointer address %p\n",myproject);
	printf("myproject flag address %p\n",&(myproject->flag));
	//now I know flag, I need to get it's parent struct
	struct mproject *internal_project = container_of(&(myproject->flag),struct mproject,flag); 
	printf("using container_of, internal_project address %p\n",internal_project);
	free(myproject);
	return 0;
}
