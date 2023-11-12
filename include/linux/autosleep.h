#include <linux/suspend.h>

suspend_state_t pm_autosleep_state(void);
int pm_autosleep_set_state(suspend_state_t state);