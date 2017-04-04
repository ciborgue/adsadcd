#include "I2CSETUP.h"
#include "ADSADC.h"

using namespace std;

// #define ADSADC_DEBUG_LOGGING

static const float PGA_VOLTAGE[] {
	6.144, 4.096, 2.048, 1.024, 0.512, 0.256, 0.256, 0.256,
};
static const char * const PGA_DESCRIPTION[] {
	"AIN0/AIN1",
	"AIN0/AIN3",
	"AIN1/AIN3",
	"AIN2/AIN3",
	"AIN0/GRND",
	"AIN1/GRND",
	"AIN2/GRND",
	"AIN3/GRND",
};
static const char * const I2C_INTERFACE[] {
	"/dev/i2c-0",
	"/dev/i2c-1",
};

ADSADC::ADSADC(I2CSETUP& setup) : i2c(setup) {}
ADSADC::~ADSADC() {}
void ADSADC::reset() {
	const char * i2cName = I2C_INTERFACE[i2c.channel];
	int generalFd;
	if ((generalFd = wiringPiI2CSetupInterface(i2cName, 0x00)) == -1) {
		throw runtime_error("can't init I2C [GENERAL]");
	}
	wiringPiI2CReadReg8(generalFd, 0x06); // send GENERAL reset
	close(generalFd);
}
void ADSADC::waitForStatus() {
	for (int i = 0; i < DEFAULT_RETRY_COUNT; i++) {
		configRegister.raw = wiringPiI2CReadReg16(fd, 0x01);
		if (configRegister.os == 1) {
			return;
		}
		usleep(DEFAULT_USEC_DELAY);
	}
	throw runtime_error("Timeout waiting for ADSADC status");
}
void ADSADC::acquireData() {
	const char * i2cName = I2C_INTERFACE[i2c.channel];
	if ((fd = wiringPiI2CSetupInterface(i2cName, i2c.address)) == -1) {
		throw runtime_error("can't open I2C bus");
	}
	// NOTE you can't add more than one ADS pet bus using this method even
	// though ADS supports addressing via pins. General reset affects ALL ADSes
	// on I2C at once and you'll need to set them up individually; this is
	// outside of the scope of this project
	reset();
	waitForStatus();
	if (configRegister.raw != 0x8385) {
		throw runtime_error("ADSADC: magic is wrong after reset;"
				" is ADS wired correctly?");
	}
	for (int mux = 0; mux < 8; mux++) { // read all possible inputs; 0..7
		for (int pga = 7; pga >= 0; pga--) {
			configRegister.mux = mux;
			configRegister.pga = pga;
			wiringPiI2CWriteReg16(fd, 0x01, configRegister.raw);
			waitForStatus();
			union {
				uint16_t  raw;
				int16_t   data;
				uint8_t   bytes[2];
			} rd;
			rd.raw = wiringPiI2CReadReg16(fd, 0x00);
			rd.raw = rd.bytes[1] | (((uint16_t) rd.bytes[0]) << 8); // reverse
			data[mux].raw = rd.data; // unsigned to signed, make g++ happy
			data[mux].pga = pga; // what GA step was used?
			// There's no good programmatic way to tell ADS1015 from ADS1115
			// (12 or 16 bit ADC). The difference that can be observed is this:
			// on overlow ADS1015 returns 0x7ff0 while ADS1115 returns 0x7fff.
			// So if I ever see any of lower 4 bits set I assume it is 1115
			// or else it is 1015.
			if (!is1115 && (rd.raw & 0xf) != 0) {
#ifdef ADSADC_DEBUG_LOGGING
				syslog(LOG_MAKEPRI(LOG_USER, LOG_DEBUG), "ADS1115 detected");
#endif
				is1115 = true;
			}
			if (rd.raw != (is1115 ? 0x7fff : 0x7ff0) && rd.raw != 0x8000) {
				// no overflow, nor underflow: stop adjusting GA and return
				break;
			}
		}
	}
	close(fd);
	tmstamp = time(NULL); // last time reading acquired
}
float ADSADC::voltage(int mux) {
	static const float max = 32752;
	static const float min = 32768;

	return  (data[mux].raw < 0 ? data[mux].raw / min : data[mux].raw / max)
		* PGA_VOLTAGE[data[mux].pga];
}
const char *ADSADC::toString() {
	strncpy(text, "tm:", sizeof text);

	int out = strlen(text);
	strftime(text + out, sizeof text - out,
			"\"%Y-%m-%d %T %z\"", localtime(&tmstamp));

	out = strlen(text);

	snprintf(text + out, sizeof text - out,
			"iam: ADS%d", is1115 ? 1115 : 1015);

	for (int i = 4; i < 8; i++) {
		out = strlen(text);
		snprintf(text + out, sizeof text - out,
				" %s[pga:%d]: %.3f", PGA_DESCRIPTION[i], data[i].pga, voltage(i));
	}

	return text;
}
const char *ADSADC::toJSON(int readings) {
	snprintf(text, sizeof text,
			"\"ADSADC%02X%02X\": {", i2c.channel, i2c.address);
	int start = strlen(text);
	strftime(text + start, sizeof text - start,
			"\"timestamp\": \"%FT%TZ\"", localtime(&tmstamp)); // for JSON syntax
	start = strlen(text);
	for (int mux = 0; mux < 8; mux++) {
		if ((readings & (1 << mux)) == 0) {
			continue;
		}
		start = strlen(text);
		snprintf(text + start, sizeof text - start,
				", \"%s\": {\"pga\": %d, \"raw\": \"0x%04x\", \"voltage\": %.5f}",
				PGA_DESCRIPTION[mux], data[mux].pga,
				(uint16_t) data[mux].raw, voltage(mux));
	}
	start = strlen(text);
	snprintf(text + start, sizeof text - start, "}");
	return text;
}
