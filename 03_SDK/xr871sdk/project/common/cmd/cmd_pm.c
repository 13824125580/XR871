/*
 * Copyright (C) 2017 XRADIO TECHNOLOGY CO., LTD. All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *    1. Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *    2. Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the
 *       distribution.
 *    3. Neither the name of XRADIO TECHNOLOGY CO., LTD. nor the names of
 *       its contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cmd_debug.h"
#include "cmd_util.h"
#include "cmd_pm.h"
#include "driver/chip/hal_wakeup.h"

#ifdef CONFIG_PM
static enum cmd_status cmd_pm_config_exec(char *cmd)
{
	int32_t cnt;
	uint32_t console, level, delayms;

	cnt = cmd_sscanf(cmd, "c=%d l=%d d=%d", &console, &level, &delayms);
	if (cnt != 3 || console > 1 || level > __TEST_AFTER_LAST || delayms > 100) {
		return CMD_STATUS_INVALID_ARG;
	}

	pm_console_set_enable(console);
	pm_set_test_level(level);
	pm_set_debug_delay_ms(delayms);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_pm_wakeuptimer_exec(char *cmd)
{
	int32_t cnt;
	uint32_t wakeup_time;

	cnt = cmd_sscanf(cmd, "%d", &wakeup_time);
	if (cnt != 1 || wakeup_time < 1) {
		return CMD_STATUS_INVALID_ARG;
	}

	HAL_Wakeup_SetTimer_mS(wakeup_time * 1000);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_pm_wakeupio_exec(char *cmd)
{
	int32_t cnt;
	uint32_t io_num, mode;

	cnt = cmd_sscanf(cmd, "p=%d m=%d", &io_num, &mode);
	if (cnt != 2 || io_num > 7 || mode > 1) {
		return CMD_STATUS_INVALID_ARG;
	}

	HAL_Wakeup_SetIO(io_num, mode);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_pm_sleep_exec(char *cmd)
{
	pm_enter_mode(PM_MODE_SLEEP);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_pm_standby_exec(char *cmd)
{
	pm_enter_mode(PM_MODE_STANDBY);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_pm_hibernation_exec(char *cmd)
{
	pm_enter_mode(PM_MODE_HIBERNATION);

	return CMD_STATUS_OK;
}

static enum cmd_status cmd_pm_poweroff_exec(char *cmd)
{
	pm_enter_mode(PM_MODE_POWEROFF);

	return CMD_STATUS_OK;
}

static struct cmd_data g_pm_cmds[] = {
	{ "config",      cmd_pm_config_exec },
	{ "wk_timer",    cmd_pm_wakeuptimer_exec },
	{ "wk_io",       cmd_pm_wakeupio_exec },
	{ "sleep",       cmd_pm_sleep_exec },
	{ "standby",     cmd_pm_standby_exec },
	{ "hibernation", cmd_pm_hibernation_exec },
	{ "poweroff",    cmd_pm_poweroff_exec },
	{ "shutdown",    cmd_pm_poweroff_exec },
};

enum cmd_status cmd_pm_exec(char *cmd)
{
	return cmd_exec(cmd, g_pm_cmds, cmd_nitems(g_pm_cmds));
}
#else
enum cmd_status cmd_pm_exec(char *cmd)
{
	return CMD_STATUS_OK;
}
#endif /* CONFIG_PM */
