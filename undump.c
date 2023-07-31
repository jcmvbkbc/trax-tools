#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	uint32_t off = 0;

	for (;;) {
		int n;
		uint32_t v;

		n = scanf("%"SCNx32, &v);
		if (n != 1)
			break;
		n = getchar();
		if (n == ':') {
			if (off != v) {
				fprintf(stderr, "bad offset: got %x expected %x\n",
					v, off);
				return 1;
			}
		} else {
			++off;
			write(STDOUT_FILENO, &v, sizeof(v));
			if (n == EOF)
				break;
		}
	}
	return 0;
}
