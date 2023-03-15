#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <stdbool.h>

#include "uci.h"
#include "bit.h"
#include "pos.h"
#include "move.h"
#include "movegen.h"

int main(void)
{
	uci_loop();

	return EXIT_SUCCESS;
}
