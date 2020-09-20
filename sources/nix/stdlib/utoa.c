/**
 * written by nonarkitten - enhanced by bebbo
 */

ALIAS( ultoa, utoa);

char* utoa(unsigned int value, char *str, int base) {
	// our digits for any base from 2-16
	static const char digits[] = "0123456789ABCDEF";
	// allocate a buffer large enough, base 2 is 32-bits + null
	char buffer[33], *b = buffer, c;
	// store our initial pointer for return value
	char *_str = str;

	// avoid stupid inputs
	if ((base > 2) && (base <= 16)) {
		// null-terminate in reverse order
		*b++ = 0;
		// copy characters in reverse order
		do
			c = digits[value % base], *b++ = c;
		while (value /= base);
		// if the user provided a good pointer
		// copy them back-to-front into user provided buffer
		if (str)
			do
				c = *--b, *str++ = c;
			while (c);
	}

	// return pointer to parameter
	return _str;
}
