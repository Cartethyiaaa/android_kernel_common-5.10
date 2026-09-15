// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/thermal/thermalcustom.c
 *
 * Standalone user-controlled thermal profile switch, independent of the
 * generic thermal_sys framework and any MediaTek thermal driver/trip
 * config. Pairs with the CONFIG_THERMAL_CUSTOM_LIMIT hook in
 * drivers/thermal/gov_step_wise.c.
 *
 * Exposes:
 *   /sys/thermalcustom/mode       (RW, 0-4, see thermalcustom_mode below)
 *   /sys/thermalcustom/temp_max   (RW, degrees Celsius, manual fine-tune)
 *   /sys/thermalcustom/enabled    (RO, whether the gov_step_wise hook is
 *                                  currently intervening at all)
 *
 * Modes:
 *   0 DEFAULT   - our hook does nothing; device's own thermal
 *                 zones/trip points behave exactly as stock.
 *   1 BALANCE   - relaxed ceiling, no governor changes. Less
 *                 aggressive than stock, but not wide open.
 *   2 POWERSAVE - tight ceiling, cooler + more battery-friendly than
 *                 even stock.
 *   3 GAMING    - wide ceiling meant to be paired with a userspace
 *                 policy (Rey Thermal or similar) that also forces
 *                 CPU governor=performance and the GPU governor, keyed
 *                 off a game/app allowlist. This driver does NOT do
 *                 app matching itself (kernel only sees UIDs, not
 *                 package names) - see the uevent below.
 *   4 DISABLE   - our hook active with the ceiling pinned at the
 *                 sysfs write max (120C) - i.e. no thermal limiting
 *                 at all short of the floor firmware/hardware cutoff.
 *
 * Every mode change fires a uevent (KOBJ_CHANGE) on the thermalcustom
 * kobject with a THERMALCUSTOM_MODE=<n> env var, so a userspace daemon
 * can listen (e.g. via a netlink uevent socket, or `udevadm monitor -k`)
 * and apply the parts that genuinely belong in userspace: CPU/GPU
 * governor switching and game/app-list matching.
 */

#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/thermalcustom.h>

#define THERMALCUSTOM_TEMP_MAX_FLOOR     20   /* refuse to go below this  */
#define THERMALCUSTOM_TEMP_MAX_CEILING  120   /* refuse to go above this  */

struct thermalcustom_preset {
	const char	*name;
	bool		enabled;    /* does the gov_step_wise hook intervene? */
	int		temp_max;   /* ceiling in degC, ignored if !enabled   */
};

static const struct thermalcustom_preset thermalcustom_presets[THERMALCUSTOM_MODE_COUNT] = {
	[THERMALCUSTOM_MODE_DEFAULT]   = { "default",   false, 45  },
	[THERMALCUSTOM_MODE_BALANCE]   = { "balance",   true,  50  },
	[THERMALCUSTOM_MODE_POWERSAVE] = { "powersave", true,  38  },
	[THERMALCUSTOM_MODE_GAMING]    = { "gaming",    true,  75  },
	[THERMALCUSTOM_MODE_DISABLE]   = { "disable",   true,  120 },
};

static DEFINE_MUTEX(thermalcustom_lock);
static enum thermalcustom_mode thermalcustom_mode = THERMALCUSTOM_MODE_DEFAULT;
static bool thermalcustom_enabled = false;
static int  thermalcustom_temp_max = 45;

static struct kobject *thermalcustom_kobj;

static void thermalcustom_notify_mode(enum thermalcustom_mode mode)
{
	char env_buf[32];
	char *envp[] = { env_buf, NULL };

	snprintf(env_buf, sizeof(env_buf), "THERMALCUSTOM_MODE=%d", mode);
	kobject_uevent_env(thermalcustom_kobj, KOBJ_CHANGE, envp);
}

/*
 * thermalcustom_get_temp_max - read the current ceiling (°C)
 *
 * Only meaningful when thermalcustom_is_enabled() is true - see
 * gov_step_wise.c, which checks both.
 */
int thermalcustom_get_temp_max(void)
{
	int val;

	mutex_lock(&thermalcustom_lock);
	val = thermalcustom_temp_max;
	mutex_unlock(&thermalcustom_lock);

	return val;
}
EXPORT_SYMBOL_GPL(thermalcustom_get_temp_max);

/*
 * thermalcustom_is_enabled - whether the gov_step_wise hook should
 * intervene at all. False in THERMALCUSTOM_MODE_DEFAULT, so trip
 * points fall through to whatever the device/vendor DT configured -
 * this is the "use the device's own built-in node" mode.
 */
bool thermalcustom_is_enabled(void)
{
	bool val;

	mutex_lock(&thermalcustom_lock);
	val = thermalcustom_enabled;
	mutex_unlock(&thermalcustom_lock);

	return val;
}
EXPORT_SYMBOL_GPL(thermalcustom_is_enabled);

enum thermalcustom_mode thermalcustom_get_mode(void)
{
	enum thermalcustom_mode val;

	mutex_lock(&thermalcustom_lock);
	val = thermalcustom_mode;
	mutex_unlock(&thermalcustom_lock);

	return val;
}
EXPORT_SYMBOL_GPL(thermalcustom_get_mode);

static void thermalcustom_apply_preset(enum thermalcustom_mode mode)
{
	const struct thermalcustom_preset *p = &thermalcustom_presets[mode];

	mutex_lock(&thermalcustom_lock);
	thermalcustom_mode     = mode;
	thermalcustom_enabled  = p->enabled;
	thermalcustom_temp_max = p->temp_max;
	mutex_unlock(&thermalcustom_lock);

	pr_info("thermalcustom: mode -> %d (%s), enabled=%d temp_max=%d\n",
		mode, p->name, p->enabled, p->temp_max);

	thermalcustom_notify_mode(mode);
}

static ssize_t mode_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	enum thermalcustom_mode mode = thermalcustom_get_mode();

	return sysfs_emit(buf, "%d %s\n", mode, thermalcustom_presets[mode].name);
}

static ssize_t mode_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if (val < 0 || val >= THERMALCUSTOM_MODE_COUNT)
		return -EINVAL;

	thermalcustom_apply_preset((enum thermalcustom_mode)val);

	return count;
}

static struct kobj_attribute mode_attr = __ATTR_RW(mode);

static ssize_t temp_max_show(struct kobject *kobj, struct kobj_attribute *attr,
			      char *buf)
{
	return sysfs_emit(buf, "%d\n", thermalcustom_get_temp_max());
}

static ssize_t temp_max_store(struct kobject *kobj, struct kobj_attribute *attr,
			       const char *buf, size_t count)
{
	int val, ret;

	ret = kstrtoint(buf, 10, &val);
	if (ret)
		return ret;

	if (val < THERMALCUSTOM_TEMP_MAX_FLOOR || val > THERMALCUSTOM_TEMP_MAX_CEILING)
		return -EINVAL;

	mutex_lock(&thermalcustom_lock);
	/* Manual override: also flips enabled on, since setting an
	 * explicit ceiling only makes sense if the hook is active. Mode
	 * stays whatever it was for display purposes - this is a
	 * fine-tune on top of it, not a mode switch.
	 */
	thermalcustom_enabled  = true;
	thermalcustom_temp_max = val;
	mutex_unlock(&thermalcustom_lock);

	pr_info("thermalcustom: temp_max manually set to %d\n", val);

	return count;
}

static struct kobj_attribute temp_max_attr = __ATTR_RW(temp_max);

static ssize_t enabled_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sysfs_emit(buf, "%d\n", thermalcustom_is_enabled());
}

static struct kobj_attribute enabled_attr = __ATTR_RO(enabled);

static struct attribute *thermalcustom_attrs[] = {
	&mode_attr.attr,
	&temp_max_attr.attr,
	&enabled_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(thermalcustom);

static int __init thermalcustom_init(void)
{
	int ret;

	/* Parent under /sys/ directly, so the path is /sys/thermalcustom/... */
	thermalcustom_kobj = kobject_create_and_add("thermalcustom", NULL);
	if (!thermalcustom_kobj)
		return -ENOMEM;

	ret = sysfs_create_groups(thermalcustom_kobj, thermalcustom_groups);
	if (ret) {
		kobject_put(thermalcustom_kobj);
		thermalcustom_kobj = NULL;
		return ret;
	}

	/* Boot in DEFAULT: fully passthrough until userspace picks a mode. */
	mutex_lock(&thermalcustom_lock);
	thermalcustom_mode     = THERMALCUSTOM_MODE_DEFAULT;
	thermalcustom_enabled  = thermalcustom_presets[THERMALCUSTOM_MODE_DEFAULT].enabled;
	thermalcustom_temp_max = thermalcustom_presets[THERMALCUSTOM_MODE_DEFAULT].temp_max;
	mutex_unlock(&thermalcustom_lock);

	pr_info("thermalcustom: initialized, mode=0 (default, passthrough)\n");

	return 0;
}

/*
 * late_initcall so it comes up after the real thermal framework/zones
 * (subsys/device_initcall) are already registered - this file is purely
 * additive and shouldn't race any thermal zone probe.
 */
late_initcall(thermalcustom_init);
