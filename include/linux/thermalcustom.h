/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _LINUX_THERMALCUSTOM_H
#define _LINUX_THERMALCUSTOM_H

enum thermalcustom_mode {
	THERMALCUSTOM_MODE_DEFAULT	= 0,
	THERMALCUSTOM_MODE_BALANCE	= 1,
	THERMALCUSTOM_MODE_POWERSAVE	= 2,
	THERMALCUSTOM_MODE_GAMING	= 3,
	THERMALCUSTOM_MODE_DISABLE	= 4,
	THERMALCUSTOM_MODE_COUNT,
};

#if IS_ENABLED(CONFIG_THERMAL_CUSTOM_LIMIT)
extern int  thermalcustom_get_temp_max(void);
extern bool thermalcustom_is_enabled(void);
extern enum thermalcustom_mode thermalcustom_get_mode(void);
#else
static inline int  thermalcustom_get_temp_max(void) { return 45; }
static inline bool thermalcustom_is_enabled(void) { return false; }
static inline enum thermalcustom_mode thermalcustom_get_mode(void)
{ return THERMALCUSTOM_MODE_DEFAULT; }
#endif

#endif /* _LINUX_THERMALCUSTOM_H */
