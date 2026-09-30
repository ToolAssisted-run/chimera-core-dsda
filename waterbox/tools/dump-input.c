/* dump-input.c - the three controllers (dsda-input.c) as JSON, for
 * gen-declaration.py: the declaration's buttons and axes are the driver's own
 * lists, in the driver's order, never a second copy */
#include <stdio.h>

#include "../dsda-input.h"

static void dump(int format, const char *name)
{
	const struct dsda_controller *c = dsda_controller(format);
	printf("\"%s\": {\"buttons\": [", name);
	for (int i = 0; i < c->nbuttons; i++) printf("%s\"%s\"", i ? ", " : "", c->buttons[i].name);
	printf("], \"axes\": [");
	for (int i = 0; i < c->naxes; i++)
		printf("%s{\"name\": \"%s\", \"min\": %d, \"max\": %d, \"neutral\": %d}", i ? ", " : "",
			c->axes[i].name, c->axes[i].min, c->axes[i].max, c->axes[i].neutral);
	printf("]}");
}

int main(void)
{
	printf("{");
	dump(FORMAT_DOOM, "doom");
	printf(", ");
	dump(FORMAT_HERETIC, "heretic");
	printf(", ");
	dump(FORMAT_HEXEN, "hexen");
	printf("}\n");
	return 0;
}
