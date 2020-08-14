/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#ifndef __DDP_PWM_H__
#define __DDP_PWM_H__

enum disp_pwm_id_t {
	DISP_PWM0 = 0x1,
	DISP_PWM1 = 0x2,
	DISP_PWM_ALL = (DISP_PWM0 | DISP_PWM1)
};

void disp_pwm_set_main(enum disp_pwm_id_t main);
enum disp_pwm_id_t disp_pwm_get_main(void);

int disp_pwm_is_enabled(enum disp_pwm_id_t id);

int disp_pwm_set_backlight(enum disp_pwm_id_t id, int level_1024);
int disp_pwm_set_backlight_cmdq(enum disp_pwm_id_t id, int level_1024,
				void *cmdq);

int disp_pwm_set_max_backlight(enum disp_pwm_id_t id, unsigned int level_1024);
int disp_pwm_get_max_backlight(enum disp_pwm_id_t id);

/* For backward compatible */
int disp_bls_set_max_backlight(unsigned int level_1024);
int disp_bls_set_backlight(int level_1024);

#endif
