#ifndef __I2CSETUP_H__
#define __I2CSETUP_H__
#include <cstdio>
#include "config.h"

class I2CSETUP {
		int fd = -1;
	public:
		int channel;
		int address;
		char text[TEXT_BUFFER_LENGTH];

		virtual const char *toString() { // Java-style
			snprintf(text, sizeof text,
				"I2C: channel:%d; address:%02x", channel, address);
			return text;
		}
};
#endif // __I2CSETUP_H__
