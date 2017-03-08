#ifndef __ADSADC_H__
#define __ADSADC_H__
#include <cstring>
#include <stdexcept>

#include <unistd.h>
#include <stdint.h>
#include <syslog.h>

#include <wiringPiI2C.h>

#include "config.h"
#include "I2CSETUP.h"

class ADSADC {
	private:
		bool is1115 = false; // assume 12bit ADC on startup
		time_t tmstamp = 0; // last reading timestamp
		union {
			uint16_t  raw = 0x8385; // note that bytes are reversed from TI docs
			struct {
				unsigned  mode:1;
				unsigned  pga:3;
				unsigned  mux:3;
				unsigned  os:1;
				unsigned  comp_que:2;
				unsigned  comp_lat:1;
				unsigned  comp_pol:1;
				unsigned  comp_mode:1;
				unsigned  dr:3;
			};
		} configRegister;
		struct {
			int     pga;
			int16_t raw;
		} data[8];
		char text[TEXT_BUFFER_LENGTH];

		float voltage(int mux);
		void waitForStatus(); // wait until conversion is done
		void reset(); // reset chip
	public:
		ADSADC(I2CSETUP&);
		~ADSADC();

		I2CSETUP i2c; // I2C sensor description
		int fd; // I2C file descriptor from wiringPi

		void acquireData(); // read data from the chip

		virtual const char *toJSON(int readings = 0xff);
		virtual const char *toString();
};
#endif // __ADSADC_H__
