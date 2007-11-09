/****************************************************************************
* Function to encode in Base64
*
* Written by Lauri Alanko
*
*****************************************************************************/

#include "common.h"
#include "base64.h"

char *
base64 (const char *bin, size_t len)
{

	char *buf = (char *) malloc ((len + 2) / 3 * 4 + 1);
	size_t i = 0, j = 0;

	char BASE64_END = '=';
	char base64_table[64];
	strncpy (base64_table, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/", 64);

	while (j < len - 2) {
		buf[i++] = base64_table[bin[j] >> 2];
		buf[i++] = base64_table[((bin[j] & 3) << 4) | (bin[j + 1] >> 4)];
		buf[i++] = base64_table[((bin[j + 1] & 15) << 2) | (bin[j + 2] >> 6)];
		buf[i++] = base64_table[bin[j + 2] & 63];
		j += 3;
	}

	switch (len - j) {
	case 1:
		buf[i++] = base64_table[bin[j] >> 2];
		buf[i++] = base64_table[(bin[j] & 3) << 4];
		buf[i++] = BASE64_END;
		buf[i++] = BASE64_END;
		break;
	case 2:
		buf[i++] = base64_table[bin[j] >> 2];
		buf[i++] = base64_table[((bin[j] & 3) << 4) | (bin[j + 1] >> 4)];
		buf[i++] = base64_table[(bin[j + 1] & 15) << 2];
		buf[i++] = BASE64_END;
		break;
	case 0:
		break;
	}

	buf[i] = '\0';
	return buf;
}

