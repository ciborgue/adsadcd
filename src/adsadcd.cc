#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>

#include <syslog.h>
#include <unistd.h>
#include <getopt.h>

#include <ctime>
#include <string>
#include <vector>
#include <stdexcept>

#include <wiringPi.h>

using namespace std;

#include "config.h"
#include "ADSADC.h"
#include "SysSem.h"

#define ADSADC_DEBUG_LOGGING

struct Receiver {
	Receiver(ADSADC *receiver, SysSem *semaphore,
			string *jsonFileName, int readings)
		: receiver(receiver), semaphore(semaphore),
		jsonFileName(jsonFileName), readings(readings) {}
	ADSADC *receiver;
	SysSem *semaphore;
	string *jsonFileName;
	int			readings; // bitmask of desired readings
};
static vector<Receiver *> receivers;
static void jsonUpdateLoop() {
	for (Receiver * r : receivers) {
		char newFile[PATH_MAX];
		snprintf(newFile, sizeof newFile, "%s.new", r->jsonFileName->c_str());

		FILE *json = fopen(newFile, "w");
		if (json == NULL) {
			syslog(LOG_ERR, "Can't open JSON file: %s", newFile);
		} else {
			fprintf(json, "{%s}\n", r->receiver->toJSON(r->readings));
			fclose(json);
			rename(newFile, r->jsonFileName->c_str());
			syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s", r->receiver->toString());
		}
	}
}
int main(int argc, char *argv[]) {
	SysSem	semaphore("/i2c");

	openlog (semaphore.imgName(),
			LOG_PERROR|LOG_CONS|LOG_PID|LOG_NDELAY, LOG_USER);
  syslog(LOG_MAKEPRI(LOG_USER, LOG_INFO), "%s", PACKAGE_STRING);

	I2CSETUP	i2c;
  int polltime = 30; // every such seconds
  int readings = 16; // AIN0 reading by default
	i2c.channel = 0x01;
	i2c.address = 0x48;

  if (wiringPiSetupSys() == -1) {
    const char * error = "can't setup wiringPi Sys mode";
    syslog(LOG_MAKEPRI(LOG_USER, LOG_ERR), "%s", error);
    throw runtime_error(error);
  }

  while (true) {
    static const struct option long_options[] = {
      {"channel",	required_argument, 0, 'c' },
      {"address",	required_argument, 0, 'a' },
      {"polltime",	required_argument, 0, 'p' },
      {"readings",	required_argument, 0, 'r' },
      {"jsonfile",	required_argument, 0, 'j' },
      {"help",		no_argument,       0, 'h' },
      {0,         	0,                 0,  0  },
    };
    int c = getopt_long(argc, argv, "c:a:p:r:j:h", long_options, NULL);
    if (c == -1) {
      break;
    }
    switch (c) {
      case 'c':
        i2c.channel = strtol(optarg, NULL, 0);
        break;
      case 'a':
        i2c.address = strtol(optarg, NULL, 0);
        break;
      case 'p':
        polltime = strtol(optarg, NULL, 0);
        break;
      case 'r':
        readings = 0;
        for (const char* p = strtok(optarg, ",");  p;  p = strtok(NULL, ",")) {
#ifdef ADSADC_DEBUG_LOGGING
					syslog(LOG_MAKEPRI(LOG_USER, LOG_DEBUG), "\tAdding rd: %s", p);
#endif
          readings |= (1 << (strtol(p, NULL, 0)));
        }
#ifdef ADSADC_DEBUG_LOGGING
				syslog(LOG_MAKEPRI(LOG_USER, LOG_DEBUG), "\tTotal rd: %d", readings);
#endif
        break;
      case 'j':
				receivers.push_back(new Receiver(
							new ADSADC(i2c), &semaphore, new string(optarg), readings));
        break;
      case 'h':
      default:
        printf("Usage: %s --polltime arg", argv[0]);
				printf(" --channel arg --address arg"
						" --readings n[,n]... --jsonfile arg\n");
				printf("\t[--channel arg --address arg"
						" --readings n[,n]... --jsonfile arg]...\n");
        exit(0);
    }
  }
  try {
    while (receivers.size() != 0) {
			for (Receiver * r : receivers) {
				semaphore.lock();
				r->receiver->acquireData();
				semaphore.unlock();
			}
      jsonUpdateLoop();
      sleep(polltime);
    }
  } catch (exception& e) {
    semaphore.unlock();
    syslog(LOG_ERR, "%s", e.what());
    throw;
  }
  return 0;
}
