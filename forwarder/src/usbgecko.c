#include <gccore.h>
#include <sys/iosupport.h>
#include "usbgecko.h"

static bool usbgecko = false;
static int geckochannel = -1;
static mutex_t usbgecko_mutex = 0;

static ssize_t _usbgecko_write(struct _reent *r, void *fd, const char *ptr, size_t len) {
	(void)r;
	(void)fd;

	if (!ptr || !len || !usbgecko)
		return 0;

	LWP_MutexLock(usbgecko_mutex);
	uint32_t level = IRQ_Disable();
	usb_sendbuffer(geckochannel, ptr, len);
	IRQ_Restore(level);
	LWP_MutexUnlock(usbgecko_mutex);

	return len;
}

static const devoptab_t dotab_geckoout = {
	"geckoout", 0, NULL, NULL, _usbgecko_write, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL,	NULL, NULL, NULL
};


void EnableUSBGecko(int channel) {
	static bool init = false;
	if (!init) {
		LWP_MutexInit(&usbgecko_mutex, false);
		init = true;
	}

	usbgecko = usb_isgeckoalive(channel);
	if (!usbgecko) return;

	devoptab_list[STD_OUT] = &dotab_geckoout;
	geckochannel = channel;
}
