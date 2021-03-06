/*
 * (C) Copyright 2009
 * Texas Instruments, <www.ti.com>
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include <common.h>
#include <asm/arch/cpu.h>
#include <asm/io.h>
#include <asm/arch/bits.h>
#include <asm/arch/mux.h>
#include <asm/arch/sys_proto.h>
#include <asm/arch/sys_info.h>
#include <asm/arch/clocks.h>
#include <asm/arch/mem.h>
#include <i2c.h>
#include <asm/mach-types.h>
#include <linux/mtd/nand_ecc.h>
#include <twl4030.h>

int get_boot_type(void);
void v7_flush_dcache_all(int, int);
void l2cache_enable(void);
void setup_auxcr(int, int);
void eth_init(void *);


/*******************************************************
 * Routine: delay
 * Description: spinning delay to use before udelay works
 ******************************************************/
static inline void delay(unsigned long loops)
{
	__asm__ volatile ("1:\n" "subs %0, %1, #1\n"
			  "bne 1b":"=r" (loops):"0"(loops));
}

/*****************************************
 * Routine: board_init
 * Description: Early hardware init.
 *****************************************/
int board_init(void)
{
	DECLARE_GLOBAL_DATA_PTR;

	gpmc_init();		/* in SRAM or SDRAM, finish GPMC */

	gd->bd->bi_arch_number = MACH_TYPE_OMAP3621_EDP1; /* Linux mach id*/
	gd->bd->bi_boot_params = (OMAP34XX_SDRC_CS0 + 0x100); /* boot param addr */

	return 0;
}

/*****************************************
 * Routine: secure_unlock
 * Description: Setup security registers for access
 * (GP Device only)
 *****************************************/
void secure_unlock_mem(void)
{
	/* Permission values for registers -Full fledged permissions to all */
	#define UNLOCK_1 0xFFFFFFFF
	#define UNLOCK_2 0x00000000
	#define UNLOCK_3 0x0000FFFF

	/* Protection Module Register Target APE (PM_RT)*/
	__raw_writel(UNLOCK_1, RT_REQ_INFO_PERMISSION_1);
	__raw_writel(UNLOCK_1, RT_READ_PERMISSION_0);
	__raw_writel(UNLOCK_1, RT_WRITE_PERMISSION_0);
	__raw_writel(UNLOCK_2, RT_ADDR_MATCH_1);

	__raw_writel(UNLOCK_3, GPMC_REQ_INFO_PERMISSION_0);
	__raw_writel(UNLOCK_3, GPMC_READ_PERMISSION_0);
	__raw_writel(UNLOCK_3, GPMC_WRITE_PERMISSION_0);

	__raw_writel(UNLOCK_3, OCM_REQ_INFO_PERMISSION_0);
	__raw_writel(UNLOCK_3, OCM_READ_PERMISSION_0);
	__raw_writel(UNLOCK_3, OCM_WRITE_PERMISSION_0);
	__raw_writel(UNLOCK_2, OCM_ADDR_MATCH_2);

	/* IVA Changes */
	__raw_writel(UNLOCK_3, IVA2_REQ_INFO_PERMISSION_0);
	__raw_writel(UNLOCK_3, IVA2_READ_PERMISSION_0);
	__raw_writel(UNLOCK_3, IVA2_WRITE_PERMISSION_0);

	__raw_writel(UNLOCK_3, IVA2_REQ_INFO_PERMISSION_1);
	__raw_writel(UNLOCK_3, IVA2_READ_PERMISSION_1);
	__raw_writel(UNLOCK_3, IVA2_WRITE_PERMISSION_1);

	__raw_writel(UNLOCK_3, IVA2_REQ_INFO_PERMISSION_2);
	__raw_writel(UNLOCK_3, IVA2_READ_PERMISSION_2);
	__raw_writel(UNLOCK_3, IVA2_WRITE_PERMISSION_2);

	__raw_writel(UNLOCK_3, IVA2_REQ_INFO_PERMISSION_3);
	__raw_writel(UNLOCK_3, IVA2_READ_PERMISSION_3);
	__raw_writel(UNLOCK_3, IVA2_WRITE_PERMISSION_3);

	__raw_writel(UNLOCK_1, SMS_RG_ATT0); /* SDRC region 0 public */
}


/**********************************************************
 * Routine: secureworld_exit()
 * Description: If chip is EMU and boot type is external
 *		configure secure registers and exit secure world
 *  general use.
 ***********************************************************/
void secureworld_exit(void)
{
	unsigned long i;

	/* configrue non-secure access control register */
	__asm__ __volatile__("mrc p15, 0, %0, c1, c1, 2":"=r" (i));
	/* enabling co-processor CP10 and CP11 accesses in NS world */
	__asm__ __volatile__("orr %0, %0, #0xC00":"=r"(i));
	/* allow allocation of locked TLBs and L2 lines in NS world */
	/* allow use of PLE registers in NS world also */
	__asm__ __volatile__("orr %0, %0, #0x70000":"=r"(i));
	__asm__ __volatile__("mcr p15, 0, %0, c1, c1, 2":"=r" (i));

	/* Enable ASA and IBE in ACR register */
	__asm__ __volatile__("mrc p15, 0, %0, c1, c0, 1":"=r" (i));
	__asm__ __volatile__("orr %0, %0, #0x50":"=r"(i));
	__asm__ __volatile__("mcr p15, 0, %0, c1, c0, 1":"=r" (i));

	/* Exiting secure world */
	__asm__ __volatile__("mrc p15, 0, %0, c1, c1, 0":"=r" (i));
	__asm__ __volatile__("orr %0, %0, #0x31":"=r"(i));
	__asm__ __volatile__("mcr p15, 0, %0, c1, c1, 0":"=r" (i));
}

/**********************************************************
 * Routine: try_unlock_sram()
 * Description: If chip is GP/EMU(special) type, unlock the SRAM for
 *  general use.
 ***********************************************************/
void try_unlock_memory(void)
{
	int mode;
	int in_sdram = running_in_sdram();

	/* if GP device unlock device SRAM for general use */
	/* secure code breaks for Secure/Emulation device - HS/E/T*/
	mode = get_device_type();
	if (mode == GP_DEVICE) {
		secure_unlock_mem();
	}
	/* If device is EMU and boot is XIP external booting
	 * Unlock firewalls and disable L2 and put chip
	 * out of secure world
	 */
	/* Assuming memories are unlocked by the demon who put us in SDRAM */
	if ((mode <= EMU_DEVICE) && (get_boot_type() == 0x1F)
		&& (!in_sdram)) {
		secure_unlock_mem();
		secureworld_exit();
	}

	return;
}

/**********************************************************
 * Routine: s_init
 * Description: Does early system init of muxing and clocks.
 * - Called path is with SRAM stack.
 **********************************************************/
void s_init(void)
{
	int i;
	int external_boot = 0;
	int in_sdram = running_in_sdram();

	watchdog_init();

	external_boot = (get_boot_type() == 0x1F) ? 1 : 0;
	/* Right now flushing at low MPU speed. Need to move after clock init */
	v7_flush_dcache_all(get_device_type(), external_boot);

	try_unlock_memory();

	if (cpu_is_3410()) {
		/* Lock down 6-ways in L2 cache so that effective size of L2 is 64K */
		__asm__ __volatile__("mov %0, #0xFC":"=r" (i));
		__asm__ __volatile__("mcr p15, 1, %0, c9, c0, 0":"=r" (i));
	}

#ifndef CONFIG_ICACHE_OFF
	icache_enable();
#endif

#ifdef CONFIG_L2_OFF
	l2cache_disable();
#else
	l2cache_enable();
#endif
	set_muxconf_regs();
	delay(100);
	
	/* Writing to AuxCR in U-boot using SMI for GP/EMU DEV */
	/* Currently SMI in Kernel on ES2 devices seems to have an isse
	 * Once that is resolved, we can postpone this config to kernel
	 */
	setup_auxcr(get_device_type(), external_boot);

	prcm_init();

	per_clocks_enable();
}

/*******************************************************
 * Routine: misc_init_r
 * Description: Init ethernet (done here so udelay works)
 ********************************************************/
int misc_init_r(void)
{
#ifdef CONFIG_DRIVER_OMAP34XX_I2C

	unsigned char data;

	i2c_init(CFG_I2C_SPEED, CFG_I2C_SLAVE);
	twl4030_power_reset_init();
	/* see if we need to activate the power button startup */
	char *s = getenv("pbboot");
	if (s) {
		/* figure out why we have booted */
		i2c_read(0x4b, 0x3a, 1, &data, 1);

		/* if status is non-zero, we didn't transition
		* from WAIT_ON state
		*/
		if (data) {
			printf("Transitioning to Wait State (%x)\n", data);

			/* clear status */
			data = 0;
			i2c_write(0x4b, 0x3a, 1, &data, 1);

			/* put PM into WAIT_ON state */
			data = 0x01;
			i2c_write(0x4b, 0x46, 1, &data, 1);

			/* no return - wait for power shutdown */
			while (1) {;}
		}
		printf("Transitioning to Active State (%x)\n", data);

		/* turn on long pwr button press reset*/
		data = 0x40;
		i2c_write(0x4b, 0x46, 1, &data, 1);
		printf("Power Button Active\n");
	}
#endif
	twl4030_keypad_init();
	dieid_num_r();

	return (0);
}

/******************************************************
 * Routine: wait_for_command_complete
 * Description: Wait for posting to finish on watchdog
 ******************************************************/
void wait_for_command_complete(unsigned int wd_base)
{
	int pending = 1;
	do {
		pending = __raw_readl(wd_base + WWPS);
	} while (pending);
}

/****************************************
 * Routine: watchdog_init
 * Description: Shut down watch dogs
 *****************************************/
void watchdog_init(void)
{
	/* There are 3 watch dogs WD1=Secure, WD2=MPU, WD3=IVA. WD1 is
	 * either taken care of by ROM (HS/EMU) or not accessible (GP).
	 * We need to take care of WD2-MPU or take a PRCM reset.  WD3
	 * should not be running and does not generate a PRCM reset.
	 */

	sr32(CM_FCLKEN_WKUP, 5, 1, 1);
	sr32(CM_ICLKEN_WKUP, 5, 1, 1);
	wait_on_value(BIT5, 0x20, CM_IDLEST_WKUP, 5); /* some issue here */

	__raw_writel(WD_UNLOCK1, WD2_BASE + WSPR);
	wait_for_command_complete(WD2_BASE);
	__raw_writel(WD_UNLOCK2, WD2_BASE + WSPR);
}

/**********************************************
 * Routine: dram_init
 * Description: sets uboots idea of sdram size
 **********************************************/
int dram_init(void)
{
    #define NOT_EARLY 0
    DECLARE_GLOBAL_DATA_PTR;
	unsigned int size0 = 0, size1 = 0;
	u32 mtype, btype;

	btype = get_board_type();
	mtype = get_mem_type();
    /* If a second bank of DDR is attached to CS1 this is
     * where it can be started.  Early init code will init
     * memory on CS0.
     */
	if ((mtype == DDR_COMBO) || (mtype == DDR_STACKED)) {
		do_sdrc_init(SDRC_CS1_OSET, NOT_EARLY);
	}
	size0 = get_sdr_cs_size(SDRC_CS0_OSET);
	size1 = get_sdr_cs_size(SDRC_CS1_OSET);

	gd->bd->bi_dram[0].start = PHYS_SDRAM_1;
	gd->bd->bi_dram[0].size = size0;
	gd->bd->bi_dram[1].start = PHYS_SDRAM_1+size0;
	gd->bd->bi_dram[1].size = size1;

	return 0;
}

#define 	MUX_VAL(OFFSET,VALUE)\
		__raw_writew((VALUE), OMAP34XX_CTRL_BASE + (OFFSET));

#define		CP(x)	(CONTROL_PADCONF_##x)
/*
 * IEN  - Input Enable
 * IDIS - Input Disable
 * PTD  - Pull type Down
 * PTU  - Pull type Up
 * DIS  - Pull type selection is inactive
 * EN   - Pull type selection is active
 * M0   - Mode 0
 * The commented string gives the final mux configuration for that pin
 */
#define MUX_DEFAULT_ES2()\
	/*SDRC*/\
	MUX_VAL(CP(SDRC_D0),        (IEN  | PTD | DIS | M0)) /*SDRC_D0*/\
	MUX_VAL(CP(SDRC_D1),        (IEN  | PTD | DIS | M0)) /*SDRC_D1*/\
	MUX_VAL(CP(SDRC_D2),        (IEN  | PTD | DIS | M0)) /*SDRC_D2*/\
	MUX_VAL(CP(SDRC_D3),        (IEN  | PTD | DIS | M0)) /*SDRC_D3*/\
	MUX_VAL(CP(SDRC_D4),        (IEN  | PTD | DIS | M0)) /*SDRC_D4*/\
	MUX_VAL(CP(SDRC_D5),        (IEN  | PTD | DIS | M0)) /*SDRC_D5*/\
	MUX_VAL(CP(SDRC_D6),        (IEN  | PTD | DIS | M0)) /*SDRC_D6*/\
	MUX_VAL(CP(SDRC_D7),        (IEN  | PTD | DIS | M0)) /*SDRC_D7*/\
	MUX_VAL(CP(SDRC_D8),        (IEN  | PTD | DIS | M0)) /*SDRC_D8*/\
	MUX_VAL(CP(SDRC_D9),        (IEN  | PTD | DIS | M0)) /*SDRC_D9*/\
	MUX_VAL(CP(SDRC_D10),       (IEN  | PTD | DIS | M0)) /*SDRC_D10*/\
	MUX_VAL(CP(SDRC_D11),       (IEN  | PTD | DIS | M0)) /*SDRC_D11*/\
	MUX_VAL(CP(SDRC_D12),       (IEN  | PTD | DIS | M0)) /*SDRC_D12*/\
	MUX_VAL(CP(SDRC_D13),       (IEN  | PTD | DIS | M0)) /*SDRC_D13*/\
	MUX_VAL(CP(SDRC_D14),       (IEN  | PTD | DIS | M0)) /*SDRC_D14*/\
	MUX_VAL(CP(SDRC_D15),       (IEN  | PTD | DIS | M0)) /*SDRC_D15*/\
	MUX_VAL(CP(SDRC_D16),       (IEN  | PTD | DIS | M0)) /*SDRC_D16*/\
	MUX_VAL(CP(SDRC_D17),       (IEN  | PTD | DIS | M0)) /*SDRC_D17*/\
	MUX_VAL(CP(SDRC_D18),       (IEN  | PTD | DIS | M0)) /*SDRC_D18*/\
	MUX_VAL(CP(SDRC_D19),       (IEN  | PTD | DIS | M0)) /*SDRC_D19*/\
	MUX_VAL(CP(SDRC_D20),       (IEN  | PTD | DIS | M0)) /*SDRC_D20*/\
	MUX_VAL(CP(SDRC_D21),       (IEN  | PTD | DIS | M0)) /*SDRC_D21*/\
	MUX_VAL(CP(SDRC_D22),       (IEN  | PTD | DIS | M0)) /*SDRC_D22*/\
	MUX_VAL(CP(SDRC_D23),       (IEN  | PTD | DIS | M0)) /*SDRC_D23*/\
	MUX_VAL(CP(SDRC_D24),       (IEN  | PTD | DIS | M0)) /*SDRC_D24*/\
	MUX_VAL(CP(SDRC_D25),       (IEN  | PTD | DIS | M0)) /*SDRC_D25*/\
	MUX_VAL(CP(SDRC_D26),       (IEN  | PTD | DIS | M0)) /*SDRC_D26*/\
	MUX_VAL(CP(SDRC_D27),       (IEN  | PTD | DIS | M0)) /*SDRC_D27*/\
	MUX_VAL(CP(SDRC_D28),       (IEN  | PTD | DIS | M0)) /*SDRC_D28*/\
	MUX_VAL(CP(SDRC_D29),       (IEN  | PTD | DIS | M0)) /*SDRC_D29*/\
	MUX_VAL(CP(SDRC_D30),       (IEN  | PTD | DIS | M0)) /*SDRC_D30*/\
	MUX_VAL(CP(SDRC_D31),       (IEN  | PTD | DIS | M0)) /*SDRC_D31*/\
	MUX_VAL(CP(SDRC_CLK),       (IEN  | PTD | DIS | M0)) /*SDRC_CLK*/\
	MUX_VAL(CP(SDRC_DQS0),      (IEN  | PTD | DIS | M0)) /*SDRC_DQS0*/\
	MUX_VAL(CP(SDRC_DQS1),      (IEN  | PTD | DIS | M0)) /*SDRC_DQS1*/\
	MUX_VAL(CP(SDRC_DQS2),      (IEN  | PTD | DIS | M0)) /*SDRC_DQS2*/\
	MUX_VAL(CP(SDRC_DQS3),      (IEN  | PTD | DIS | M0)) /*SDRC_DQS3*/\
	/* MUX_VAL(CP(SYS_NRESWARM),   (IDIS | PTD | DIS | M0)) */ /*SYS_NRESWARM*/\
	/*GPMC*/\
	MUX_VAL(CP(GPMC_A1),        (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*GPMC_A1*/\
	MUX_VAL(CP(GPMC_A2),        (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-35 */\
	MUX_VAL(CP(GPMC_A3),        (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-36 */\
	MUX_VAL(CP(GPMC_A4),        (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-37 */\
	MUX_VAL(CP(GPMC_A5),        (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-38 */\
	MUX_VAL(CP(GPMC_A6),        (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-39 */\
	MUX_VAL(CP(GPMC_A7),        (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GIO-40 */\
	MUX_VAL(CP(GPMC_A8),        (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-41 */\
	MUX_VAL(CP(GPMC_A9),        (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-42 */\
	MUX_VAL(CP(GPMC_A10),       (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GIO-43 */\
	MUX_VAL(CP(GPMC_D0),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D0*/\
	MUX_VAL(CP(GPMC_D1),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D1*/\
	MUX_VAL(CP(GPMC_D2),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D2*/\
	MUX_VAL(CP(GPMC_D3),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D3*/\
	MUX_VAL(CP(GPMC_D4),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D4*/\
	MUX_VAL(CP(GPMC_D5),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D5*/\
	MUX_VAL(CP(GPMC_D6),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D6*/\
	MUX_VAL(CP(GPMC_D7),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D7*/\
	MUX_VAL(CP(GPMC_D8),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D8*/\
	MUX_VAL(CP(GPMC_D9),        (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D9*/\
	MUX_VAL(CP(GPMC_D10),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D10*/\
	MUX_VAL(CP(GPMC_D11),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D11*/\
	MUX_VAL(CP(GPMC_D12),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D12*/\
	MUX_VAL(CP(GPMC_D13),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D13*/\
	MUX_VAL(CP(GPMC_D14),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D14*/\
	MUX_VAL(CP(GPMC_D15),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_D15*/\
	MUX_VAL(CP(GPMC_nCS0),      (OFF_OUT_PD | IDIS | PTU | EN  | M0)) /*GPMC_nCS0*/\
	MUX_VAL(CP(GPMC_nCS7),      (OFF_OUT_PD | IDIS | PTU | EN  | M0)) /*GPMC_IODIR*/\
	MUX_VAL(CP(GPMC_CLK),       (OFF_OUT_PD | IEN  | PTD | DIS | M0)) /*GPMC_CLK*/\
	MUX_VAL(CP(GPMC_nADV_ALE),  (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*GPMC_nADV_ALE*/\
	MUX_VAL(CP(GPMC_nOE),       (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*GPMC_nOE*/\
	MUX_VAL(CP(GPMC_nWE),       (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*GPMC_nWE*/\
	MUX_VAL(CP(GPMC_nBE0_CLE),  (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*GPMC_nBE0_CLE*/\
	MUX_VAL(CP(GPMC_nBE1),      (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_nBE1*/\
	MUX_VAL(CP(GPMC_nWP),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*GPMC_nWP*/\
	MUX_VAL(CP(GPMC_WAIT0),     (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*GPMC_WAIT0*/\
	/*DSS*/\
	MUX_VAL(CP(DSS_PCLK),       (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_PCLK*/\
	MUX_VAL(CP(DSS_HSYNC),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_HSYNC*/\
	MUX_VAL(CP(DSS_VSYNC),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_VSYNC*/\
	MUX_VAL(CP(DSS_ACBIAS),     (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_ACBIAS*/\
	MUX_VAL(CP(DSS_DATA0),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA0 */\
	MUX_VAL(CP(DSS_DATA1),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA1 */\
	MUX_VAL(CP(DSS_DATA2),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA2 */\
	MUX_VAL(CP(DSS_DATA3),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA3 */\
	MUX_VAL(CP(DSS_DATA4),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA4 */\
	MUX_VAL(CP(DSS_DATA5),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA5 */\
	MUX_VAL(CP(DSS_DATA6),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA6 */\
	MUX_VAL(CP(DSS_DATA7),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA7 */\
	MUX_VAL(CP(DSS_DATA8),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA8*/\
	MUX_VAL(CP(DSS_DATA9),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA9*/\
	MUX_VAL(CP(DSS_DATA10),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-80*/\
	MUX_VAL(CP(DSS_DATA11),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-81*/\
	MUX_VAL(CP(DSS_DATA12),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-82*/\
	MUX_VAL(CP(DSS_DATA13),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-83*/\
	MUX_VAL(CP(DSS_DATA14),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-84*/\
	MUX_VAL(CP(DSS_DATA15),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-85*/\
	MUX_VAL(CP(DSS_DATA16),     (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GIO-86*/\
	MUX_VAL(CP(DSS_DATA17),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-87*/\
	MUX_VAL(CP(DSS_DATA18),     (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GIO-2*/\
	MUX_VAL(CP(DSS_DATA19),     (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-3*/\
	MUX_VAL(CP(DSS_DATA20),     (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA20*/\
	MUX_VAL(CP(DSS_DATA21),     (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GIO-6*/\
	MUX_VAL(CP(DSS_DATA22),     (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA22*/\
	MUX_VAL(CP(DSS_DATA23),     (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*DSS_DATA23*/\
	/*CAMERA*/\
	MUX_VAL(CP(CAM_XCLKA),      (OFF_OUT_PD | IEN  | PTD | EN  | M4)) /*GIO-96*/\
	MUX_VAL(CP(CAM_D0 ),        (OFF_IN_PD  | IEN  | PTD | DIS | M2)) /*CAM_D0 */\
	MUX_VAL(CP(CAM_D1 ),        (OFF_IN_PD  | IEN  | PTD | DIS | M2)) /*CAM_D1 */\
	MUX_VAL(CP(CAM_D2 ),        (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-101*/\
	MUX_VAL(CP(CAM_D3 ),        (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-102*/\
	MUX_VAL(CP(CAM_D4 ),        (OFF_IN_PD  | IEN  | PTD | DIS | M4)) /*GIO-103*/\
	MUX_VAL(CP(CAM_D5 ),        (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-104*/\
	MUX_VAL(CP(CAM_D10),        (OFF_IN_PD  | IEN  | PTD | DIS | M4)) /*GIO-109*/\
	MUX_VAL(CP(CAM_D11),        (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-110*/\
	MUX_VAL(CP(CAM_XCLKB),      (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GIO-111*/\
	MUX_VAL(CP(CSI2_DX0),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*CSI2_DX0*/\
	MUX_VAL(CP(CSI2_DY0),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*CSI2_DY0*/\
	MUX_VAL(CP(CSI2_DX1),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*CSI2_DX1*/\
	MUX_VAL(CP(CSI2_DY1),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*CSI2_DY1*/\
	/*Audio Interface */\
	MUX_VAL(CP(McBSP2_FSX),     (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*McBSP2_FSX*/\
	MUX_VAL(CP(McBSP2_CLKX),    (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*McBSP2_CLKX*/\
	MUX_VAL(CP(McBSP2_DR),      (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*McBSP2_DR*/\
	MUX_VAL(CP(McBSP2_DX),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*McBSP2_DX*/\
	/*Expansion card  */\
	MUX_VAL(CP(MMC1_CLK),       (OFF_OUT_PD | IEN  | PTU | EN  | M0)) /*MMC1_CLK */\
	MUX_VAL(CP(MMC1_CMD),       (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_CMD */\
	MUX_VAL(CP(MMC1_DAT0),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT0*/\
	MUX_VAL(CP(MMC1_DAT1),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT1*/\
	MUX_VAL(CP(MMC1_DAT2),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT2*/\
	MUX_VAL(CP(MMC1_DAT3),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT3*/\
	MUX_VAL(CP(MMC1_DAT4),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT4*/\
	MUX_VAL(CP(MMC1_DAT5),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT5*/\
	MUX_VAL(CP(MMC1_DAT6),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT6*/\
	MUX_VAL(CP(MMC1_DAT7),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC1_DAT7*/\
	/* eMMC */\
	MUX_VAL(CP(MMC2_CLK),       (OFF_IN_PD  | IEN  | PTD | EN  | M0)) /*MMC2_CLK */\
	MUX_VAL(CP(MMC2_CMD),       (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC2_CMD */\
	MUX_VAL(CP(MMC2_DAT0),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC2_DAT0*/\
	MUX_VAL(CP(MMC2_DAT1),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC2_DAT1*/\
	MUX_VAL(CP(MMC2_DAT2),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC2_DAT2*/\
	MUX_VAL(CP(MMC2_DAT3),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC2_DAT3*/\
	MUX_VAL(CP(MMC2_DAT4),      (OFF_IN_PD  | IEN  | PTD | EN  | M0)) /*MMC2_DIR_DAT0*/\
	MUX_VAL(CP(MMC2_DAT5),      (OFF_IN_PD  | IEN  | PTD | EN  | M0)) /*MMC2_DIR_DAT1*/\
	MUX_VAL(CP(MMC2_DAT6),      (OFF_IN_PD  | IEN  | PTD | EN  | M0)) /*MMC2_DIR_CMD */\
	MUX_VAL(CP(MMC2_DAT7),      (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*MMC2_CLKIN*/\
	/*Bluetooth*/\
	MUX_VAL(CP(McBSP3_DX),      (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*McBSP3_DX  */\
	MUX_VAL(CP(McBSP3_DR),      (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*McBSP3_DR  */\
	MUX_VAL(CP(McBSP3_CLKX),    (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*McBSP3_CLKX*/\
	MUX_VAL(CP(McBSP3_FSX),     (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*McBSP3_FSX */\
	/*Modem Interface */\
	MUX_VAL(CP(UART1_TX),       (OFF_OUT_PD | IDIS | PTD | DIS | M0)) /*UART1_TX*/\
	MUX_VAL(CP(UART1_RTS),      (OFF_OUT_PD | IDIS | PTD | DIS | M7)) /*UART1_RTS*/\
	MUX_VAL(CP(UART1_CTS),      (OFF_IN_PD  | IEN  | PTU | DIS | M7)) /*UART1_CTS*/\
	MUX_VAL(CP(UART1_RX),       (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*UART1_RX*/\
	MUX_VAL(CP(McBSP1_CLKR),    (OFF_IN_PD  | IEN  | PTD | DIS | M4)) /*GIO-156     */\
	MUX_VAL(CP(McBSP1_FSR),     (OFF_OUT_PD | IEN  | PTU | EN  | M4)) /*GPIO_157    */\
	MUX_VAL(CP(McBSP1_DX),      (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GIO-158     */\
	MUX_VAL(CP(McBSP1_DR),      (OFF_IN_PD  | IEN  | PTD | DIS | M4)) /*GIO-159     */\
	MUX_VAL(CP(McBSP1_FSX),     (OFF_IN_PD  | IEN  | PTD | DIS | M4)) /*GIO-161     */\
	/*Serial Interface*/\
	MUX_VAL(CP(HSUSB0_CLK),     (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_CLK*/\
	MUX_VAL(CP(HSUSB0_STP),     (OFF_OUT_PD | IDIS | PTU | EN  | M0)) /*HSUSB0_STP*/\
	MUX_VAL(CP(HSUSB0_DIR),     (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DIR*/\
	MUX_VAL(CP(HSUSB0_NXT),     (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_NXT*/\
	MUX_VAL(CP(HSUSB0_DATA0),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA0 */\
	MUX_VAL(CP(HSUSB0_DATA1),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA1 */\
	MUX_VAL(CP(HSUSB0_DATA2),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA2 */\
	MUX_VAL(CP(HSUSB0_DATA3),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA3 */\
	MUX_VAL(CP(HSUSB0_DATA4),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA4 */\
	MUX_VAL(CP(HSUSB0_DATA5),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA5 */\
	MUX_VAL(CP(HSUSB0_DATA6),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA6 */\
	MUX_VAL(CP(HSUSB0_DATA7),   (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*HSUSB0_DATA7 */\
	MUX_VAL(CP(I2C1_SCL),       (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*I2C1_SCL*/\
	MUX_VAL(CP(I2C1_SDA),       (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*I2C1_SDA*/\
	MUX_VAL(CP(I2C2_SCL),       (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*I2C2_SCL*/\
	MUX_VAL(CP(I2C2_SDA),       (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*I2C2_SDA*/\
	MUX_VAL(CP(I2C4_SCL),       (             IEN  | PTU | EN  | M0)) /*I2C4_SCL*/\
	MUX_VAL(CP(I2C4_SDA),       (             IEN  | PTU | EN  | M0)) /*I2C4_SDA*/\
	MUX_VAL(CP(McSPI1_CS3),     (OFF_IN_PD  | IEN  | PTD | EN  | M3)) /*HSUSB2_D2*/\
	MUX_VAL(CP(McSPI2_CLK),     (OFF_IN_PD  | IEN  | PTD | DIS | M3)) /*HSUSB2_D7*/\
	MUX_VAL(CP(McSPI2_SIMO),    (OFF_IN_PD  | IEN  | PTD | DIS | M3)) /*HSUSB2_D4*/\
	MUX_VAL(CP(McSPI2_SOMI),    (OFF_IN_PD  | IEN  | PTD | DIS | M3)) /*HSUSB2_D5*/\
	MUX_VAL(CP(McSPI2_CS0),     (OFF_IN_PD  | IEN  | PTD | EN  | M3)) /*HSUSB2_D6*/\
	MUX_VAL(CP(McSPI2_CS1),     (OFF_IN_PD  | IEN  | PTD | EN  | M3)) /*HSUSB2_D3*/\
	/*Control and debug */\
	MUX_VAL(CP(SYS_32K),        (             IEN  | PTD | DIS | M0)) /*SYS_32K*/\
	MUX_VAL(CP(SYS_CLKREQ),     (             IEN  | PTD | DIS | M0)) /*SYS_CLKREQ*/\
	MUX_VAL(CP(SYS_nIRQ),       (OFF_IN_PD  | IEN  | PTU | EN  | M0)) /*SYS_nIRQ*/\
	MUX_VAL(CP(SYS_BOOT0),      (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GPIO_2 */\
	MUX_VAL(CP(SYS_BOOT1),      (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GPIO_3 */\
	MUX_VAL(CP(SYS_BOOT2),      (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GPIO_4 */\
	MUX_VAL(CP(SYS_BOOT3),      (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GPIO_5 */\
	MUX_VAL(CP(SYS_BOOT4),      (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GPIO_6 */\
	MUX_VAL(CP(SYS_BOOT5),      (OFF_OUT_PD | IEN  | PTD | DIS | M4)) /*GPIO_7 */\
	MUX_VAL(CP(SYS_BOOT6),      (OFF_OUT_PD | IDIS | PTD | DIS | M4)) /*GPIO_8 */\
	MUX_VAL(CP(SYS_OFF_MODE),   (             IEN  | PTD | DIS | M0)) /*SYS_OFF_MODE */\
	MUX_VAL(CP(SYS_CLKOUT2),    (OFF_IN_PD  | IDIS | PTU | EN  | M0)) /*SYS_CLKOUT2	 */\
	MUX_VAL(CP(JTAG_nTRST),     (             IEN  | PTD | DIS | M0)) /*JTAG_nTRST*/\
	MUX_VAL(CP(JTAG_TCK),       (             IEN  | PTD | DIS | M0)) /*JTAG_TCK*/\
	MUX_VAL(CP(JTAG_TMS),       (             IEN  | PTD | DIS | M0)) /*JTAG_TMS*/\
	MUX_VAL(CP(JTAG_TDI),       (             IEN  | PTD | DIS | M0)) /*JTAG_TDI*/\
	MUX_VAL(CP(JTAG_TDO),  	    (             IEN  | PTD | DIS | M0)) /*JTAG_TDO*/\
	MUX_VAL(CP(JTAG_RTCK),      (             IEN  | PTD | DIS | M0)) /*JTAG_RTCK*/\
	MUX_VAL(CP(JTAG_EMU0),      (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*JTAG_EMU0*/\
	MUX_VAL(CP(JTAG_EMU1),      (OFF_IN_PD  | IEN  | PTD | DIS | M0)) /*JTAG_EMU1*/\
	MUX_VAL(CP(ETK_CTL_ES2),    (OFF_OUT_PD | IEN  | PTD | DIS | M2)) /*MMC3_CMD */\
	MUX_VAL(CP(ETK_CLK_ES2),    (             IEN  | PTU | EN  | M2)) /*MMC3_CLK  */\
	MUX_VAL(CP(ETK_D0_ES2 ),    (OFF_IN_PD  | IEN  | PTD | DIS | M4)) /*GIO-14    */\
	MUX_VAL(CP(ETK_D1_ES2 ),    (OFF_IN_PD  | IEN  | PTD | DIS | M4)) /*GIO-15    */\
	MUX_VAL(CP(ETK_D2_ES2 ),    (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-16    */\
	MUX_VAL(CP(ETK_D4_ES2 ),    (             IEN  | PTU | EN  | M2)) /*MMC3_DAT0 */\
	MUX_VAL(CP(ETK_D5_ES2 ),    (             IEN  | PTU | EN  | M2)) /*MMC3_DAT1 */\
	MUX_VAL(CP(ETK_D6_ES2 ),    (             IEN  | PTU | EN  | M2)) /*MMC3_DAT2 */\
	MUX_VAL(CP(ETK_D3_ES2 ),    (             IEN  | PTU | EN  | M2)) /*MMC3_DAT3 */\
	MUX_VAL(CP(ETK_D7_ES2 ),    (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-21    */\
	MUX_VAL(CP(ETK_D8_ES2 ),    (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-22    */\
	MUX_VAL(CP(ETK_D9_ES2 ),    (OFF_IN_PD  | IDIS | PTD | DIS | M4)) /*GIO-23    */\
	MUX_VAL(CP(ETK_D10_ES2),    (OFF_IN_PD  | IDIS | PTD | DIS | M3)) /*HSUSB2_CLK*/\
	MUX_VAL(CP(ETK_D11_ES2),    (OFF_IN_PD  | IDIS | PTD | DIS | M3)) /*HSUSB2_STP*/\
	MUX_VAL(CP(ETK_D12_ES2),    (OFF_IN_PD  | IEN  | PTD | DIS | M3)) /*HSUSB2_DIR*/\
	MUX_VAL(CP(ETK_D13_ES2),    (OFF_IN_PD  | IEN  | PTD | DIS | M3)) /*HSUSB2_NXT*/\
	MUX_VAL(CP(ETK_D14_ES2),    (OFF_IN_PD  | IEN  | PTD | DIS | M3)) /*HSUSB2_D0 */\
	MUX_VAL(CP(ETK_D15_ES2),    (OFF_IN_PD  | IEN  | PTD | DIS | M3)) /*HSUSB2_D1 */\
	/*Die to Die */\
	MUX_VAL(CP(sdrc_cke0),      (             IDIS | PTU | EN  | M0)) /*sdrc_cke0 */\
	MUX_VAL(CP(sdrc_cke1),      (             IDIS | PTD | DIS | M7)) /*sdrc_cke1 not used*/

#define MUX_TFT_ES2()\
	/*DSS*/\
	MUX_VAL(CP(DSS_PCLK),       (IDIS | PTD | DIS | M0)) /*DSS_PCLK  */\
	MUX_VAL(CP(DSS_HSYNC),      (IDIS | PTD | DIS | M0)) /*DSS_HSYNC */\
	MUX_VAL(CP(DSS_VSYNC),      (IDIS | PTD | DIS | M0)) /*DSS_VSYNC */\
	MUX_VAL(CP(DSS_ACBIAS),     (IDIS | PTD | DIS | M0)) /*DSS_ACBIAS*/\
	MUX_VAL(CP(DSS_DATA0),      (IDIS | PTD | DIS | M0)) /*DSS_DATA0 */\
	MUX_VAL(CP(DSS_DATA1),      (IDIS | PTD | DIS | M0)) /*DSS_DATA1 */\
	MUX_VAL(CP(DSS_DATA2),      (IDIS | PTD | DIS | M0)) /*DSS_DATA2 */\
	MUX_VAL(CP(DSS_DATA3),      (IDIS | PTD | DIS | M0)) /*DSS_DATA3 */\
	MUX_VAL(CP(DSS_DATA4),      (IDIS | PTD | DIS | M0)) /*DSS_DATA4 */\
	MUX_VAL(CP(DSS_DATA5),      (IDIS | PTD | DIS | M0)) /*DSS_DATA5 */\
	MUX_VAL(CP(DSS_DATA6),      (IDIS | PTD | DIS | M0)) /*DSS_DATA6 */\
	MUX_VAL(CP(DSS_DATA7),      (IDIS | PTD | DIS | M0)) /*DSS_DATA7 */\
	MUX_VAL(CP(DSS_DATA8),      (IDIS | PTD | DIS | M0)) /*DSS_DATA8 */\
	MUX_VAL(CP(DSS_DATA9),      (IDIS | PTD | DIS | M0)) /*DSS_DATA9 */\
	MUX_VAL(CP(DSS_DATA10),     (IDIS | PTD | DIS | M0)) /*DSS_DATA10*/\
	MUX_VAL(CP(DSS_DATA11),     (IDIS | PTD | DIS | M0)) /*DSS_DATA11*/\
	MUX_VAL(CP(DSS_DATA12),     (IDIS | PTD | DIS | M0)) /*DSS_DATA12*/\
	MUX_VAL(CP(DSS_DATA13),     (IDIS | PTD | DIS | M0)) /*DSS_DATA13*/\
	MUX_VAL(CP(DSS_DATA14),     (IDIS | PTD | DIS | M0)) /*DSS_DATA14*/\
	MUX_VAL(CP(DSS_DATA15),     (IDIS | PTD | DIS | M0)) /*DSS_DATA15*/\
	MUX_VAL(CP(DSS_DATA16),     (IDIS | PTD | DIS | M0)) /*DSS_DATA16*/\
	MUX_VAL(CP(DSS_DATA17),     (IDIS | PTD | DIS | M0)) /*DSS_DATA17*/\
	MUX_VAL(CP(DSS_DATA18),     (IDIS | PTD | DIS | M0)) /*DSS_DATA18*/\
	MUX_VAL(CP(DSS_DATA19),     (IDIS | PTD | DIS | M0)) /*DSS_DATA19*/\
	MUX_VAL(CP(DSS_DATA20),     (IDIS | PTD | DIS | M0)) /*DSS_DATA20*/\
	MUX_VAL(CP(DSS_DATA21),     (IDIS | PTD | DIS | M0)) /*DSS_DATA21*/\
	MUX_VAL(CP(DSS_DATA22),     (IDIS | PTD | DIS | M0)) /*DSS_DATA22*/\
	MUX_VAL(CP(DSS_DATA23),     (IDIS | PTD | DIS | M0)) /*DSS_DATA23*/\
	MUX_VAL(CP(I2C2_SCL),       (IEN  | PTU | EN  | M0)) /*I2C2_SCL  */\
	MUX_VAL(CP(I2C2_SDA),       (IEN  | PTU | EN  | M0)) /*I2C2_SDA  */\
	MUX_VAL(CP(McSPI2_CLK),     (IEN  | PTD | DIS | M0)) /*hsusb2_d7 lab*/\
	MUX_VAL(CP(McSPI2_SIMO),    (IEN  | PTD | DIS | M0)) /*hsusb2_d4 lab*/\
	MUX_VAL(CP(McSPI2_SOMI),    (IEN  | PTD | DIS | M0)) /*hsusb2_d5 lab*/\
	MUX_VAL(CP(McSPI2_CS0),     (IEN  | PTD | DIS | M0)) /*hsusb2_d6 lab*/\
	MUX_VAL(CP(McSPI2_CS1),     (IEN  | PTD | DIS | M0)) /*hsusb2_d3 lab*/\
	MUX_VAL(CP(GPMC_nCS4),      (IDIS | PTD | DIS | M4)) /*GIO-55 */\
	MUX_VAL(CP(CAM_XCLKA),      (OFF_OUT_PD | IEN  | PTD | EN  | M4)) /*GIO-96*/\
	MUX_VAL(CP(UART3_CTS_RCTX), (IEN  | PTD | EN  | M4)) /*GIO-163 M4*/



/**********************************************************
 * Routine: set_muxconf_regs
 * Description: Setting up the configuration Mux registers
 *              specific to the hardware. Many pins need
 *              to be moved from protect to primary mode.
 *********************************************************/
void set_muxconf_regs(void)
{
	MUX_DEFAULT_ES2();
#ifdef CFG_TFT_DISPLAY 
	MUX_TFT_ES2();
#endif
}

/******************************************************************************
 * Routine: update_mux()
 * Description:Update balls which are different between boards.  All should be
 *             updated to match functionality.  However, I'm only updating ones
 *             which I'll be using for now.  When power comes into play they
 *             all need updating.
 *****************************************************************************/
void update_mux(u32 btype, u32 mtype)
{
	/* NOTHING as of now... */
}

