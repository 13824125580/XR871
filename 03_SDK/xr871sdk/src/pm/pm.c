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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sys/io.h"
#include "errno.h"
#include "sys/list.h"
#include "sys/interrupt.h"
#include "sys/param.h"
#include "kernel/os/os_thread.h"

#include "driver/chip/system_chip.h"
#include "driver/chip/chip.h"
#include "driver/chip/hal_wakeup.h"
#include "driver/chip/hal_prcm.h"
#include "driver/chip/hal_ccm.h"

#include "pm/pm.h"
#include "pm_i.h"
#include "port.h"

#ifdef CONFIG_PM

#define isb() __asm__ __volatile__ ("isb" : : : "memory")
#define dsb() __asm__ __volatile__ ("dsb" : : : "memory")
#define dmb() __asm__ __volatile__ ("dmb" : : : "memory")
#define wfi() __asm__ __volatile__ ("wfi" : : : "memory")
#define wfe() __asm__ __volatile__ ("wfe" : : : "memory")

static struct arm_CMX_core_regs vault_arm_registers;

static const char *const pm_states[PM_MODE_MAX] = {
	[PM_MODE_ON]            = "on",
	[PM_MODE_SLEEP]         = "sleep",
	[PM_MODE_STANDBY]       = "standby",
	[PM_MODE_HIBERNATION]   = "hibernation",
	[PM_MODE_POWEROFF]      = "poweroff",
};

static int __suspend_begin(enum suspend_state_t state)
{
	/* set SEVONPEND flag */
	SCB->SCR = 0x10;
	return 0;
}

static inline void __record_dbg_status(int val)
{
	HAL_PRCM_SetCPUAPrivateData(val);
	dsb();
	isb();
}

/* power off whole system, save user data to flash before call this func.
 * BE SUURE: all hardware has been closed and it's prcm config setted to default value.
 * @type:0:shutdown, 1:reboot */
static void pm_power_off(pm_operate_t type)
{
	__record_dbg_status(PM_POWEROFF | 0);

	if (type == PM_REBOOT) {
		NVIC_SystemReset(); /* never return */
	}

#ifdef __CONFIG_ARCH_APP_CORE
	/* step 1 & 2 has been done when wlan sys poweroff */
	/* step3: writel(0x0f, GPRCM_SYS1_WAKEUP_CTRL) to tell PMU that turn on
	 * SW1, SW2, SRSW1, LDO before release application system reset signal. */
	HAL_PRCM_SetSys1WakeupPowerFlags(0x0f);
	__record_dbg_status(PM_POWEROFF | 5);

	/* step4: writel(0x0f, GPRCM_SYS1_SLEEP_CTRL) to tell PMU that turn off SW1,
	 * SW3 SRSW1, LDO after pull down application system reset signal. */
	HAL_PRCM_SetSys1SleepPowerFlags(0x0f);
	__record_dbg_status(PM_POWEROFF | 7);

	/* step5: switch to HOSC, close SYS1_CLK. */
	SystemDeInit();
	__record_dbg_status(PM_POWEROFF | 9);

	/* step6: set nvic deepsleep flag, and enter wfe. */
	SCB->SCR = 0x14;

	__disable_fault_irq();
	__disable_irq();

	while (1) {
		wfe();
	}
	__record_dbg_status(PM_POWEROFF | 0x0ff);

#else /* net cpu */
	/* close wifi, will called by app close wifi #### */
	xradio_wlan_power(0);

	/* step1: cpu to switch to HOSC */
	HAL_PRCM_SetCPUNClk(PRCM_CPU_CLK_SRC_HFCLK, PRCM_SYS_CLK_FACTOR_80M);
	__record_dbg_status(PM_POWEROFF | 1);

	/* step2: turn off SYSCLK2. */
	HAL_PRCM_DisableSysClk2(PRCM_SYS_CLK_FACTOR_80M);
	__record_dbg_status(PM_POWEROFF | 3);
	SystemDeInit();

	/* step3: enter WFI state */
	arch_suspend_disable_irqs();
	while (1)
		wfi();
	__record_dbg_status(PM_POWEROFF | 0x0ff);
#endif
}

static void __suspend_enter(enum suspend_state_t state)
{
#if 0 /* pll/div to 32K */
	#define GPRCM_SYSCLK1_CTRL 0x40040024
	#define CCM_CPU_BUS_CLKCFG 0x40040400
	unsigned int cpu_clk_back = readl(GPRCM_SYSCLK1_CTRL);
	unsigned int bus_clk_back = readl(CCM_CPU_BUS_CLKCFG);
#endif

	if (state == PM_MODE_POWEROFF) {
#ifdef __CONFIG_ARCH_APP_CORE
		HAL_Wakeup_SetIOHold((1 << WAKEUP_IO_MAX) - 1);
#endif
		pm_power_off(PM_SHUTDOWN); /* never return */
	}

	__record_dbg_status(PM_SUSPEND_ENTER | 5);

	if (HAL_Wakeup_SetSrc())
		return ;

#if 0 /* pll/div to 32K */
	writel(((bus_clk_back & ~(0x3<<4)) | (1<<4)), CCM_CPU_BUS_CLKCFG); /* 32K */
	dsb();
	isb();

	writel(((cpu_clk_back & ((1<<31)|0x0F)) | (1<<16)), GPRCM_SYSCLK1_CTRL); /* 32K */
	dsb();
	isb();
#endif

	debug_jtag_deinit();
	__record_dbg_status(PM_SUSPEND_ENTER | 6);

	PM_LOGN("device info. rst:%x clk:%x\n", CCM->BUS_PERIPH_RST_CTRL, CCM->BUS_PERIPH_CLK_CTRL); /* debug info. */

	if (state < PM_MODE_STANDBY) {
		/* TODO: set system bus to low freq */
		__cpu_sleep(state);
		/* TODO: restore system bus to normal freq */
	} else {
		__cpu_suspend(state);
	}

	PM_BUG_ON(!arch_local_save_flags());

	debug_jtag_init();

#if 0 /* 32K to pll */
	writel(cpu_clk_back & ((1<<31)|0x0F), GPRCM_SYSCLK1_CTRL); /* 32K */
	dsb();
	isb();
	writel(cpu_clk_back, GPRCM_SYSCLK1_CTRL); /* pll/div */

	writel(bus_clk_back, CCM_CPU_BUS_CLKCFG);
#endif
	HAL_Wakeup_ClrSrc();
}

static void __suspend_end(enum suspend_state_t state)
{
	/* clear SEVONPEND flag */
	SCB->SCR = 0x0;
}

static const struct platform_suspend_ops suspend_ops =
{
	.begin = __suspend_begin,
	.enter = __suspend_enter,
	.wake = platform_wake,
	.end = __suspend_end,
};

static volatile int user_sel_power_mode = PM_MODE_ON;

static int valid_state(enum suspend_state_t state)
{
	if (user_sel_power_mode == PM_MODE_ON)
		return 0;

	return (user_sel_power_mode == state);
}

#ifdef CONFIG_PM_DEBUG
static int pm_test_level = TEST_NONE;

static unsigned long suspend_test_start_time;

static void suspend_test_start(void)
{
	suspend_test_start_time = ktime_get();
}

static void suspend_test_finish(const char *label)
{
	long nj = ktime_get() - suspend_test_start_time;
	unsigned msec;

	msec = ktime_to_msecs(abs(nj));
	PM_LOGN("%s took %d ms\n", label, msec);
}

static int suspend_test(int level)
{
	if (pm_test_level == level) {
		PM_LOGD("suspend debug:%d Return.\n", level);
		return 1;
	}
	return 0;
}

void pm_set_test_level(enum suspend_test_level_t level)
{
	pm_test_level = level;
}

void pm_dump_regs(unsigned int flag)
{
	if (flag & 1<<0) { /* cpu */
		int i, j;

		PM_LOGD("regs:\n");
		PM_LOGD("msp:0x%08x, psp:0x%08x, psr:0x%08x, primask:0x%08x\n",
			vault_arm_registers.msp, vault_arm_registers.psp, vault_arm_registers.psr,
			vault_arm_registers.primask);
		PM_LOGD("faultmask:0x%08x, basepri:0x%08x, control:0x%08x\n",
			vault_arm_registers.faultmask, vault_arm_registers.basepri,
			vault_arm_registers.control);
		for (i = 0; i < 3; i++) {
			for (j = 0; j < 4; j++) {
				PM_LOGD("reg[%d]:0x%08x ", j+i*4, vault_arm_registers.reg12[j+i*4]);
			}
			PM_LOGD("\n");
		}
		PM_LOGD("last step:%x\n", HAL_PRCM_GetCPUAPrivateData());
	}
	if (flag & 1<<1) { /* nvic */
		//nvic_print_regs();
	}
	if (flag & 1<<2) { /* ccmu */
		//ccmu_print_regs();
	}
	if (flag & 1<<3) { /* gpio */
		//gpio_print_regs();
	}
}

static void dpm_show_time(ktime_t starttime, enum suspend_state_t state, char *info)
{
	ktime_t calltime;

	calltime = ktime_get();
	PM_LOGD("%s of devices complete after %d ms\n", info ? info : "", (int)(calltime - starttime));
}

#else
#define suspend_test_start()
#define suspend_test_finish(x...)
#define suspend_test(l)         0
void pm_set_test_level(enum suspend_test_level_t level) {;}
#define pm_dump_regs(flag)
#define dpm_show_time(x...)
#endif

static LIST_HEAD_DEF(dpm_late_early_list);
static LIST_HEAD_DEF(dpm_noirq_list);

static LIST_HEAD_DEF(dpm_list);
static LIST_HEAD_DEF(dpm_suspended_list);

#ifdef CONFIG_PM_DEBUG
static struct suspend_stats suspend_stats;

void parse_dpm_list(struct list_head *head, unsigned int idx)
{
	struct list_head *list;
	struct soc_device *dev;

	PM_DBG("(%p)", head);
	list_for_each(list, head) {
		dev = to_device(list, idx);
		PM_DBG("-->%s(%p)", dev->name, dev);
	}
	PM_DBG("\n");
}
#endif

static unsigned int initcall_debug_delay_ms = 0;

void pm_set_debug_delay_ms(unsigned int ms)
{
	initcall_debug_delay_ms = ms;
}

/**
 * check_wakeup_irqs - check if any wake-up interrupts are pending
 */
int check_wakeup_irqs(void)
{
	int i;
	unsigned int *addr;
	unsigned int val;

	/* Check if there are any interrupts pending */
	addr = (unsigned int *)NVIC->ISPR;
	for (i = 0; i < DIV_ROUND_UP(NVIC_PERIPH_IRQ_NUM, 32); i++) {
		val= addr[i];
		if (val & nvic_int_mask[i]) {
			PM_LOGN("nvic[%d]:%x, mask:%x\n", i, val, nvic_int_mask[i]);
			return 1;
		}
	}

	return 0;
}

/**
 * dpm_suspend_noirq - Execute "noirq suspend" callbacks for all devices.
 * @state: PM transition of the system being carried out.
 *
 * Prevent device drivers from receiving interrupts and call the "noirq" suspend
 * handlers for all non-sysdev devices.
 */
static int dpm_suspend_noirq(enum suspend_state_t state)
{
	struct soc_device *dev = NULL;
#ifdef CONFIG_PM_DEBUG
	ktime_t starttime = ktime_get();
#endif
	int error = 0;

	//suspend_device_irqs();
	while (!list_empty(&dpm_late_early_list)) {
		dev = to_device(dpm_late_early_list.next, PM_OP1);

		get_device(dev);

		error = dev->driver->suspend_noirq(dev, state);
		if (initcall_debug_delay_ms > 0){
			PM_LOGD("sleep %d ms for debug.\n", initcall_debug_delay_ms);
			loop_delay(initcall_debug_delay_ms);
		}

		if (error || !arch_local_save_flags()) {
			PM_LOGE("%s suspend noirq failed! primask:%d\n",
			        dev->name, (int)arch_local_save_flags());
			put_device(dev);
			break;
		}
		list_move(&dev->node[PM_OP1], &dpm_noirq_list);
		dsb();
		isb();
		put_device(dev);

		if (check_wakeup_irqs()) {
			error = -1;
			break;
		}
	}

#ifdef CONFIG_PM_DEBUG
	if (error) {
		suspend_stats.failed_suspend_noirq++;
		if (dev && dev->name)
			memcpy(suspend_stats.failed_devs, dev->name, MAX_DEV_NAME);
		suspend_stats.last_failed_step = TEST_DEVICES;
		dpm_show_time(starttime, state, "noirq");
	}
#endif

	return error;
}

/**
 * dpm_suspend - Execute "suspend" callbacks for all devices.
 * @state: PM transition of the system being carried out.
 */
static int dpm_suspend(enum suspend_state_t state)
{
	struct soc_device *dev = NULL;
#ifdef CONFIG_PM_DEBUG
	ktime_t starttime = ktime_get();
#endif
	int error = 0;

	while (!list_empty(&dpm_list)) {
		dev = to_device(dpm_list.next, PM_OP0);

		get_device(dev);
		error = dev->driver->suspend(dev, state);
		if (initcall_debug_delay_ms > 0){
			PM_LOGD("sleep %d ms for debug.\n", initcall_debug_delay_ms);
			loop_delay(initcall_debug_delay_ms);
		}

		if (error) {
			PM_LOGE("%s suspend failed!\n", dev->name);
			put_device(dev);
			break;
		}
		list_move(&dev->node[PM_OP0], &dpm_suspended_list);
		dsb();
		isb();
		put_device(dev);
	}

#ifdef CONFIG_PM_DEBUG
	if (error) {
		suspend_stats.failed_suspend++;
		if (dev && dev->name)
			memcpy(suspend_stats.failed_devs, dev->name, MAX_DEV_NAME);
		suspend_stats.last_failed_step = TEST_DEVICES;
		dpm_show_time(starttime, state, "suspend");
	}
#endif

	return error;
}

/**
 * dpm_resume_noirq - Execute "noirq resume" callbacks for all devices.
 * @state: PM transition of the system being carried out.
 *
 * Call the "noirq" resume handlers for all devices in dpm_noirq_list and
 * enable device drivers to receive interrupts.
 */
static void dpm_resume_noirq(enum suspend_state_t state)
{
	struct soc_device *dev;
	ktime_t starttime = ktime_get();
	int error;

	while (!list_empty(&dpm_noirq_list)) {
		dev = to_device(dpm_noirq_list.next, PM_OP1);

		get_device(dev);
		list_move(&dev->node[PM_OP1], &dpm_late_early_list);
		dsb();
		isb();

		error = dev->driver->resume_noirq(dev, state);
#ifdef CONFIG_PM_DEBUG
		if (error) {
			suspend_stats.failed_resume_noirq++;
			PM_LOGE("%s resume noirq failed!\n", dev->name);
		}
#endif

		put_device(dev);
	}
	dpm_show_time(starttime, state, "noirq");
	//resume_device_irqs();
}

/**
 * dpm_resume - Execute "resume" callbacks for non-sysdev devices.
 * @state: PM transition of the system being carried out.
 *
 * Execute the appropriate "resume" callback for all devices whose status
 * indicates that they are suspended.
 */
static void dpm_resume(enum suspend_state_t state)
{
	struct soc_device *dev;
	ktime_t starttime = ktime_get();
	int error;

	while (!list_empty(&dpm_suspended_list)) {
		dev = to_device(dpm_suspended_list.next, PM_OP0);

		get_device(dev);
		list_move(&dev->node[PM_OP0], &dpm_list);
		dsb();
		isb();

		error = dev->driver->resume(dev, state);
#ifdef CONFIG_PM_DEBUG
		if (error) {
			suspend_stats.failed_resume++;
			PM_LOGE("%s resume failed!\n", dev->name);
		}
#endif

		put_device(dev);
	}
	dpm_show_time(starttime, state, "resume");
}

/**
 * suspend_enter - Make the system enter the given sleep state.
 * @state: System sleep state to enter.
 * @wakeup: Returns information that the sleep state should not be re-entered.
 *
 * This function should be called after devices have been suspended.
 */
static int suspend_enter(enum suspend_state_t state)
{
	int wakeup;
	int error;

	arch_suspend_disable_irqs();

	wakeup = check_wakeup_irqs();
	if (wakeup) {
		error = -1;
		goto finished;
	}

	__record_dbg_status(PM_SUSPEND_DEVICES | 0x10);
	suspend_test_start();
	error = dpm_suspend_noirq(state);
	if (error) {
#ifdef CONFIG_PM_DEBUG
		suspend_stats.fail++;
#endif
		PM_LOGE("Some devices noirq failed to suspend\n");
		parse_dpm_list(&dpm_noirq_list, PM_OP1);
		goto Resume_noirq_devices;
	}

	suspend_test_finish("suspend noirq devices");
	if (suspend_test(TEST_DEVICES))
		goto Resume_noirq_devices;

	__record_dbg_status(PM_SUSPEND_ENTER);

	__record_dbg_status(PM_SUSPEND_ENTER | 1);
	if (suspend_test(TEST_PLATFORM))
		goto Platform_wake;

	__record_dbg_status(PM_SUSPEND_ENTER | 2);
	wakeup = check_wakeup_irqs();
	if (!(suspend_test(TEST_CORE) || wakeup)) {
		__record_dbg_status(PM_SUSPEND_ENTER | 3);
		suspend_ops.enter(state);
		__record_dbg_status(PM_SUSPEND_ENTER | 4);
	}

Platform_wake:
	if (suspend_ops.wake)
		suspend_ops.wake(state);

Resume_noirq_devices:
	__record_dbg_status(PM_RESUME_DEVICES | 0x10);
	suspend_test_start();
	dpm_resume_noirq(state);
	suspend_test_finish("resume noirq devices");

finished:
	arch_suspend_enable_irqs();

	return wakeup;
}

/**
 * suspend_devices_and_enter - Suspend devices and enter system sleep state.
 * @state: System sleep state to enter.
 */
static int suspend_devices_and_enter(enum suspend_state_t state)
{
	int error;

	__record_dbg_status(PM_EARLY_SUSPEND);
	//PM_LOGD("%s %p\n", __func__, &vault_arm_registers);
	if (!valid_state(state)) {
		return -1;
	}

	__record_dbg_status(PM_SUSPEND_BEGIN);
	if (suspend_ops.begin) {
		error = suspend_ops.begin(state);
		if (error)
			goto Close;
	}

	suspend_console();

	__record_dbg_status(PM_SUSPEND_DEVICES);
	suspend_test_start();
	error = dpm_suspend(state);
	if (error) {
#ifdef CONFIG_PM_DEBUG
		suspend_stats.fail++;
#endif
		PM_LOGE("Some devices failed to suspend\n");
		goto Resume_devices;
	}
#ifdef CONFIG_PM_DEBUG
	suspend_stats.success++;
#endif
	suspend_test_finish("suspend devices");
	if (suspend_test(TEST_DEVICES))
		goto Resume_devices;

	OS_ThreadSuspendScheduler();
	__record_dbg_status(PM_SUSPEND_ENTER);
	suspend_enter(state);
	OS_ThreadResumeScheduler();

Resume_devices:
	__record_dbg_status(PM_RESUME_DEVICES);
	suspend_test_start();
	dpm_resume(state);
	suspend_test_finish("resume devices");

	resume_console();
Close:
	__record_dbg_status(PM_RESUME_END);
	if (suspend_ops.end)
		suspend_ops.end(state);
	__record_dbg_status(PM_RESUME_COMPLETE);

	return error;
}

/**
 * register_syscore_ops - Register a set of system core operations.
 * @ops: System core operations to register.
 * NOTE: not use printf for this func maybe called very earlier.
 */
int pm_register_ops(struct soc_device *dev)
{
	struct list_head *hd;
	struct soc_device *dev_c;
	unsigned long flags;
	unsigned int valid;

	if (!dev)
		return -EINVAL;

	valid = dev->node[PM_OP0].next || dev->node[PM_OP0].prev;
	if (valid)
		PM_BUG_ON(!list_empty(&dev->node[PM_OP0]));
	valid = dev->node[PM_OP1].next || dev->node[PM_OP1].prev;
	if (valid)
		PM_BUG_ON((!list_empty(&dev->node[PM_OP1])));
	PM_BUG_ON(!dev->driver || ((!dev->driver->suspend_noirq || !dev->driver->resume_noirq) &&
	          (!dev->driver->suspend || !dev->driver->resume)));

	if (dev->driver->suspend || dev->driver->resume) {
		PM_BUG_ON(!dev->driver->suspend || !dev->driver->resume);
		list_for_each(hd, &dpm_list) {
			dev_c = to_device(hd, PM_OP0);
			if (dev_c == dev) {
				goto next;
			}
		}

		INIT_LIST_HEAD(&dev->node[PM_OP0]);
		flags = xr_irq_save();
		list_add(&dev->node[PM_OP0], &dpm_list);
		xr_irq_restore(flags);
	}

next:
	if (dev->driver->suspend_noirq || dev->driver->resume_noirq) {
		PM_BUG_ON(!dev->driver->suspend_noirq || !dev->driver->resume_noirq);
		list_for_each(hd, &dpm_late_early_list) {
			dev_c = to_device(hd, PM_OP1);
			if (dev_c == dev) {
				return -1;
			}
		}

		INIT_LIST_HEAD(&dev->node[PM_OP1]);
		flags = xr_irq_save();
		list_add(&dev->node[PM_OP1], &dpm_late_early_list);
		xr_irq_restore(flags);
	}

	return 0;
}

int pm_unregister_ops(struct soc_device *dev)
{
	unsigned long flags;

	if (!dev)
		return -EINVAL;

	if (dev->driver->suspend) {
		PM_BUG_ON(!dev->node[PM_OP0].next || !dev->node[PM_OP0].prev);
		PM_BUG_ON(list_empty(&dev->node[PM_OP0]));
	}
	if (dev->driver->suspend_noirq) {
		PM_BUG_ON(!dev->node[PM_OP1].next || !dev->node[PM_OP1].prev);
		PM_BUG_ON((list_empty(&dev->node[PM_OP1])));
	}

	flags = xr_irq_save();
	list_del(&dev->node[PM_OP0]);
	list_del(&dev->node[PM_OP1]);
	xr_irq_restore(flags);

	return 0;
}

void pm_select_mode(enum suspend_state_t state)
{
	user_sel_power_mode = state;
}

#ifdef CONFIG_PM_DEBUG
void pm_stats_show(void)
{
	PM_LOGN("suspend state:\n"
	        "  success:%d\n"
	        "  fail:%d\n"
	        "  failed_suspend:%d\n"
	        "  failed_resume:%d\n"
	        "  last_failed_step:%d\n"
	        "  last_failed_step:%s\n",
	        suspend_stats.success, suspend_stats.fail, suspend_stats.failed_suspend,
	        suspend_stats.failed_resume, suspend_stats.last_failed_step, suspend_stats.failed_devs);
}
#else
void pm_stats_show(void)
{
}
#endif

/**
 * Initialize the PM-related part of a device object.
 * NOTE: not use printf for this fun is called very earlier.
 */
int pm_init(void)
{
	HAL_Wakeup_Init();

#ifdef __CONFIG_ARCH_APP_CORE
#if 0
	HAL_PRCM_EnableSys2Power();
	udelay(10000);
	HAL_PRCM_DisableSys2Power();
	udelay(10000);
#endif

	/* set prcm to default value for prcm keep it's last time value. */
	HAL_PRCM_SetSys1WakeupPowerFlags(0x0f);
	HAL_PRCM_SetSys1SleepPowerFlags(0x0e);
	HAL_PRCM_Start();
#endif

	return 0;
}

#ifdef __CONFIG_ARCH_APP_CORE
static pm_wlan_power_onoff pm_wlan_power_onoff_cb = NULL;
static int pm_wlan_mode_platform_config = PM_SUPPORT_HIBERNATION | PM_SUPPORT_POWEROFF;

int pm_register_wlan_power_onoff(pm_wlan_power_onoff wlan_power_cb, unsigned int select)
{
	if ((select & (PM_SUPPORT_HIBERNATION | PM_SUPPORT_POWEROFF)) !=
	    (PM_SUPPORT_HIBERNATION | PM_SUPPORT_POWEROFF)) {
		PM_LOGW("wlan should power off when hibernateion/poweroff!\n");
		return -1;
	}
	pm_wlan_power_onoff_cb = wlan_power_cb;
	pm_wlan_mode_platform_config = select | PM_SUPPORT_HIBERNATION | PM_SUPPORT_POWEROFF;
	PM_LOGN("wlan mode:%x\n", pm_wlan_mode_platform_config);

	return 0;
}

void pm_unregister_wlan_power_onoff(void)
{
	pm_wlan_power_onoff_cb = NULL;
}
#endif

#ifdef __CONFIG_ARCH_APP_CORE
static int pm_mode_platform_config = PM_SUPPORT_SLEEP | PM_SUPPORT_STANDBY | PM_SUPPORT_POWEROFF;

void pm_mode_platform_select(unsigned int select)
{
	pm_mode_platform_config = select;
	if (!(select & (PM_SUPPORT_SLEEP | PM_SUPPORT_STANDBY | \
	    PM_SUPPORT_HIBERNATION | PM_SUPPORT_POWEROFF))) {
		PM_LOGW("slect wrong mode!\n");
		return ;
	}
	PM_LOGN("mode select:%x\n", select);
}

static int pm_wlan_alive_platform_config = PM_SUPPORT_SLEEP | PM_SUPPORT_STANDBY;

int pm_wlan_alive_platform_select(unsigned int select)
{
	if (select & (PM_SUPPORT_HIBERNATION | PM_SUPPORT_POWEROFF)) {
		PM_LOGW("net can't be alive when hiberantion or poweroff!\n");
		return -1;
	}
	pm_wlan_alive_platform_config = select;
	PM_LOGN("net alive select:%x\n", select);

	return 0;
}
#endif

int pm_enter_mode(enum suspend_state_t state)
{
	int err, record;
	enum suspend_state_t state_use = state;
	int net_alive;
#ifdef __CONFIG_ARCH_APP_CORE
	int loop;

	if (!(pm_mode_platform_config & (1 << state))) {
		for (loop = (1 << state_use); loop; loop >>= 1) {
			if (pm_mode_platform_config & loop) {
				break;
			}
			state_use--;
		}
	}
#endif

	PM_BUG_ON(state_use >= PM_MODE_MAX);

	if (state_use < PM_MODE_SLEEP)
		return 0;

	pm_select_mode(state_use);
	PM_LOGD("appos enter mode: %s\n", pm_states[state_use]);
	record = HAL_PRCM_GetCPUAPrivateData();
	if (record != PM_RESUME_COMPLETE)
		PM_LOGN("last suspend record:%x\n", record);
	parse_dpm_list(&dpm_list, PM_OP0);  /* debug info. */
	parse_dpm_list(&dpm_late_early_list, PM_OP1);

	net_alive = HAL_PRCM_IsCPUNReleased();
	if (net_alive && (pm_wlan_mode_platform_config & (1 << state_use)) && pm_wlan_power_onoff_cb) {
		pm_wlan_power_onoff_cb(0);
	}

	HAL_PRCM_SetCPUABootArg((uint32_t)&vault_arm_registers);

	err = suspend_devices_and_enter(state_use);

	HAL_PRCM_SetCPUABootArg(PM_SYNC_MAGIC); /* set flag to notify net to run */

	if (net_alive && (pm_wlan_mode_platform_config & (1 << state_use)) && pm_wlan_power_onoff_cb) {
		pm_wlan_power_onoff_cb(1);
	}

	return err;
}

//#define CONFIG_PM_TEST 1

#ifdef CONFIG_PM_TEST

static int test_suspend_noirq(struct soc_device *dev, enum suspend_state_t state)
{
	PM_LOGD("%s %s okay\n", dev->name, __func__);

	//if (!strcmp(dev->name, "dev_test1"))
	//	return -1;
	//else
		return 0;
}

static int test_resume_noirq(struct soc_device *dev, enum suspend_state_t state)
{
	PM_LOGD("%s %s okay\n", dev->name, __func__);

	return 0;
}

static int test_suspend(struct soc_device *dev, enum suspend_state_t state)
{
	PM_LOGD("%s %s okay\n", dev->name, __func__);

	//if (!strcmp(dev->name, "dev_test1"))
	//	return -1;
	//else
		return 0;
}

static int test_resume(struct soc_device *dev, enum suspend_state_t state)
{
	PM_LOGD("%s %s okay\n", dev->name, __func__);

	return 0;
}

static struct soc_device_driver drv_test1 = {
	.name = "drv_test1",
	.suspend = &test_suspend,
	.resume = &test_resume,
};
static struct soc_device dev_test1 = {
	.name = "dev_test1",
	.driver = &drv_test1,
};

static struct soc_device_driver drv_test2 = {
	.name = "drv_test2",
	.suspend = &test_suspend,
	.resume = &test_resume,
};
static struct soc_device dev_test2 = {
	.name = "dev_test2",
	.driver = &drv_test2,
};

static struct soc_device_driver drv_test3 = {
	.name = "drv_test3",
	.suspend_noirq = &test_suspend_noirq,
	.resume_noirq = &test_resume_noirq,
	.suspend = &test_suspend,
	.resume = &test_resume,
};
static struct soc_device dev_test3 = {
	.name = "dev_test3",
	.driver = &drv_test3,
};

int pm_test(void)
{
	struct list_head *hd = &dpm_list;
	struct soc_device *dev;

	pm_register_ops(&dev_test1);
	pm_register_ops(&dev_test2);
	pm_register_ops(&dev_test2);
	pm_register_ops(&dev_test3);

	parse_dpm_list(&dpm_list, PM_OP0);
	parse_dpm_list(&dpm_late_early_list, PM_OP1);

	PM_LOGD("dpm_list:\n");
	while (!list_is_last(hd, &dpm_list)) {
		dev = to_device(hd->next, PM_OP0);
		hd = hd->next;
		PM_LOGD("name: %s\n", dev->name);
	}

	PM_LOGD("dpm_late_early_list:\n");
	hd = &dpm_late_early_list;
	while (!list_is_last(hd, &dpm_late_early_list)) {
		dev = to_device(hd->next, PM_OP1);
		hd = hd->next;
		PM_LOGD("name: %s\n", dev->name);
	}

	return 0;
}
#endif

#else /* CONFIG_PM */

int check_wakeup_irqs(void)
{
	return 0;
}

#endif /* CONFIG_PM */
