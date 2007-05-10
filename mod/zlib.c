#include <stdlib.h>
#include <string.h>
#include <zlib.h>


int encode (void* mc,
		void* dst,
		unsigned int* pdstlen,
		const void* src,
		unsigned int srclen)
{
	int		ret = -1;
	int		level = (int)(mc);
	unsigned long	uldstlen = 0;

	if (dst==NULL && src==NULL) return -1;
	uldstlen = *pdstlen;
	ret = compress2 (dst, &uldstlen, src, srclen, level>0 && level<9 ? level : 9);
	*pdstlen = (int)uldstlen;
	return ret==Z_OK ? 0 : -1;
}

int decode (void* mc,
		void* dst,
		unsigned int* pdstlen,
		const void* src,
		unsigned int srclen)
{
	int ret = -1;
	unsigned long	uldstlen = 0;

	if (dst==NULL && src==NULL) return -1;
	uldstlen = *pdstlen;
	ret = uncompress (dst, &uldstlen, src, srclen);
	*pdstlen = (int)uldstlen;
	return ret==Z_OK ? 0 : -1;
}

void* mod_init (char* args)
{
	char*	argsdup = NULL;
	char* 	arg = NULL;
	char**	argv = NULL;
	int	argcnt = 0;
	int	i = 0;
	char*	key = NULL;
	char*	val = NULL;
	int	level = 9;

	// strtok() will attempt to change the content of args
	// which will cause a "Segmentation fault" error
	if (args==NULL || (argsdup=strdup(args))==NULL) return (void*)9;
	if ((arg=strtok(argsdup,","))==NULL) {
		free(argsdup);
		return (void*)9;
	}
	do {
		argv = realloc(argv, (argcnt+1)*sizeof(char*));
		argv[argcnt] = strdup (arg);
		argcnt++;
	} while ((arg = strtok (NULL, ",")) != NULL);

	for (i=0; i<argcnt; i++) {
		key = strtok(argv[i],"=");
		if (key == NULL) continue;
		if (strcmp(key, "level") == 0 || strcmp(key, "l") == 0) {
			val = strtok (NULL, "=");
			if (val == NULL) continue;
			level = atoi(val);
			level = (level>0 && level<=9) ? level : 9;
		}
		free(argv[i]);
	}
	free(argv);
	free(argsdup);
	return (void*)9;
}

void mod_finit (void* mc) {}
