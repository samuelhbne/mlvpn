#include <stdio.h>
#include <dlfcn.h>


int main (int argc, char** argv)
{
	void* (*mod_init) (char* args);
	
	void (*mod_finit) (void* mc);
	
	int (*encode) (void* mc,
			void* dst,
			unsigned int* pdstlen,
			const void* src,
			unsigned int srclen);
	
	int (*decode) (void* mc,
			void* dst,
			unsigned int* pdstlen,
			const void* src,
			unsigned int srclen);

	void*		hmod;
	void*		mc;
	char		rawbuf[65536];
	char		encbuf[65536];
	unsigned int	rawlen;
	unsigned int	enclen;
	int		ret;

	if (argc < 2) {
		fprintf (stderr, "%s <mod frawlename>\n", argv[0]);
		return 1;
	}
	fprintf (stdout, "argv[0]: %s\n", argv[0]);
	fprintf (stdout, "argv[1]: %s\n", argv[1]);
	fflush (stdout);

	hmod = dlopen (argv[1], RTLD_NOW|RTLD_GLOBAL);
	if (!hmod) {
		fprintf (stderr, "%s\n", dlerror());
		return 1;
	}
	dlerror();
	mod_init = dlsym (hmod, "mod_init");
	mod_finit = dlsym (hmod, "mod_finit");
	encode = dlsym (hmod, "encode");
	decode = dlsym (hmod, "decode");
	if (!mod_init || !mod_finit || !encode || !decode) {
		fprintf (stderr, "%s\n", dlerror());
		return 1;
	}
	fprintf (stdout, "ready\n");
	mc = mod_init (NULL);
	if (mc == NULL) {
		perror ("mod_init");
		return 1;
	}
	enclen = sizeof(encbuf);
	ret = encode (mc, encbuf, &enclen, rawbuf, 1024);
	fprintf (stdout, "enc len is %d\n", enclen);
	rawlen = sizeof(rawbuf);
	ret = decode (mc, rawbuf, &rawlen, encbuf, enclen);
	fprintf (stdout, "raw len is %d\n", rawlen);
	dlclose(hmod);
	return 0;
}
