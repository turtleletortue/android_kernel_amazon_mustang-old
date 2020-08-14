#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt


#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/kobject.h>
#include <linux/slab.h>

#include "mt-plat/mtk_thermal_monitor.h"
#include <linux/platform_data/mtk_thermal.h>

#define virtual_sensor_cooler_backlight_dprintk(fmt, args...) pr_debug("thermal/cooler/backlight " fmt, ##args)

#define MAX_BRIGHTNESS		255
#define MAX_NUM_COOLER_LCD_BACKLIGHT 2

#if (defined(CONFIG_THERMAL_abc123) || defined(CONFIG_THERMAL_abc123))
#define CUR_STATE0_BRIGHTNESS 255
#define CUR_STATE1_BRIGHTNESS 200
#define CUR_STATE2_BRIGHTNESS 165
#define CUR_STATE3_BRIGHTNESS 90
#endif

static struct mutex bl_update_lock;
static struct mtk_cooler_platform_data cool_dev[MAX_NUM_COOLER_LCD_BACKLIGHT] = {
	{
		.type = "lcd-backlight0",
		.state = 0,
		.max_state = THERMAL_MAX_TRIPS,
		.level = MAX_BRIGHTNESS,
		.levels = {
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS,
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS,
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS,
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS
		},
	},
	{
		.type = "lcd-backlight1",
		.state = 0,
		.max_state = THERMAL_MAX_TRIPS,
		.level = MAX_BRIGHTNESS,
		.levels = {
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS,
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS,
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS,
			MAX_BRIGHTNESS, MAX_BRIGHTNESS, MAX_BRIGHTNESS
		},
	},
};

struct cooler_sort_list backlight_list_head;

static int virtual_sensor_get_max_state(struct thermal_cooling_device *cdev,
			   unsigned long *state)
{
	struct mtk_cooler_platform_data *pdata = cdev->devdata;
	*state = pdata->max_state;
	return 0;
}

static int virtual_sensor_get_cur_state(struct thermal_cooling_device *cdev,
			   unsigned long *state)
{
	struct mtk_cooler_platform_data *pdata = cdev->devdata;
	*state = pdata->state;
	return 0;
}

static int virtual_sensor_set_cur_state(struct thermal_cooling_device *cdev,
			   unsigned long state)
{
	int level;
	unsigned long max_state;
	struct mtk_cooler_platform_data *pdata = cdev->devdata;

	if (!cdev)
		return 0;

	mutex_lock(&(bl_update_lock));

	if (pdata->state == state)
		goto out;

	max_state = pdata->max_state;
	pdata->state = (state > max_state) ? max_state : state;
#if (defined(CONFIG_THERMAL_abc123) || defined(CONFIG_THERMAL_abc123))
	switch ((int)pdata->state)
	{
		case 1:
			level = CUR_STATE1_BRIGHTNESS;
			break;
		case 2:
			level = CUR_STATE2_BRIGHTNESS;
			break;
		case 3:
			level = CUR_STATE3_BRIGHTNESS;
			break;
		default:
			level = CUR_STATE0_BRIGHTNESS;
			break;
	}
#else
	if (!pdata->state)
		level = MAX_BRIGHTNESS;
	else
		level = pdata->levels[pdata->state - 1];

	if (level == pdata->level)
		goto out;

	pdata->level = level;

	level = thermal_level_compare(pdata, &backlight_list_head, true);
#endif

	setMaxbrightness(level, 1);

out:
	mutex_unlock(&(bl_update_lock));

	return 0;
}

static ssize_t levels_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct thermal_cooling_device *cdev =
		container_of(dev, struct thermal_cooling_device, device);
	struct mtk_cooler_platform_data *pdata = cdev->devdata;
	int i;
	int offset = 0;

	if (!pdata->cdev)
		return -EINVAL;
	for (i = 0; i < THERMAL_MAX_TRIPS; i++)
		offset += sprintf(buf + offset, "%d %d\n", i+1, pdata->levels[i]);
	return offset;
}

static ssize_t levels_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int level, state;
	struct thermal_cooling_device *cdev =
		container_of(dev, struct thermal_cooling_device, device);
	struct mtk_cooler_platform_data *pdata = cdev->devdata;

	if (!pdata->cdev)
		return -EINVAL;
	if (sscanf(buf, "%d %d\n", &state, &level) != 2)
		return -EINVAL;
	if (state >= THERMAL_MAX_TRIPS)
		return -EINVAL;
	pdata->levels[state] = level;
	return count;
}

static struct thermal_cooling_device_ops cooling_ops = {
	.get_max_state = virtual_sensor_get_max_state,
	.get_cur_state = virtual_sensor_get_cur_state,
	.set_cur_state = virtual_sensor_set_cur_state,
};

static DEVICE_ATTR(levels, S_IRUGO | S_IWUSR, levels_show, levels_store);

static int virtual_sensor_cooler_backlight_register_ltf(void)
{
        int i;
	struct mtk_cooler_platform_data *pdata = NULL;
	struct cooler_sort_list *tz_list = NULL;

	virtual_sensor_cooler_backlight_dprintk("register ltf\n");

	INIT_LIST_HEAD(&backlight_list_head.list);

	for (i=0; i<MAX_NUM_COOLER_LCD_BACKLIGHT; i++) {
		pdata = cool_dev+i;
 		pdata->cdev = thermal_cooling_device_register(pdata->type,
						    pdata,
						    &cooling_ops);
		if (!pdata->cdev) {
			pr_err("%s Failed to create backlight cooling device\n",
				__func__);
			return -EINVAL;
		}

		device_create_file(&pdata->cdev->device, &dev_attr_levels);

		tz_list = kzalloc(sizeof(struct cooler_sort_list), GFP_KERNEL);
		tz_list->pdata = pdata;
		list_add(&tz_list->list, &backlight_list_head.list);
        }

	mutex_init(&(bl_update_lock));



	return 0;
}

static void virtual_sensor_cooler_backlight_unregister_ltf(void)
{
	int i;
	struct mtk_cooler_platform_data *pdata = NULL;
	struct cooler_sort_list *cooler_list, *tmp;

	virtual_sensor_cooler_backlight_dprintk("unregister ltf\n");

        for (i=0; i<MAX_NUM_COOLER_LCD_BACKLIGHT; i++) {
		pdata = cool_dev+i;
 		thermal_cooling_device_unregister(pdata->cdev);
        }


	list_for_each_entry_safe(cooler_list, tmp, &backlight_list_head.list, list) {
		list_del(&cooler_list->list);
		kfree(cooler_list);
	}

	cooler_list = NULL;

	mutex_destroy(&(bl_update_lock));
}


static int __init virtual_sensor_cooler_backlight_init(void)
{
	int err = 0;

	virtual_sensor_cooler_backlight_dprintk("init\n");

	err = virtual_sensor_cooler_backlight_register_ltf();
	if (err)
		goto err_unreg;

	return 0;

 err_unreg:
	virtual_sensor_cooler_backlight_unregister_ltf();
	return err;
}

static void __exit virtual_sensor_cooler_backlight_exit(void)
{
	virtual_sensor_cooler_backlight_dprintk("exit\n");

	virtual_sensor_cooler_backlight_unregister_ltf();
}
module_init(virtual_sensor_cooler_backlight_init);
module_exit(virtual_sensor_cooler_backlight_exit);
