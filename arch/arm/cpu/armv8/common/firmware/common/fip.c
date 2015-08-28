
/*
 * arch/arm/cpu/armv8/common/firmware/common/fip.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <arch.h>
#include <arch_helpers.h>
#include <fip.h>
#include <storage.h>
#include <string.h>
#include <platform.h>
#include <platform_def.h>
#include <stdio.h>
#include <asm/arch/cpu_config.h>
#include <storage.h>
#include <sha2.h>
#include <mailbox.h>
#include <asm/arch/romboot.h>
#include <cache.h>
#include <fip.h>
#include <asm/arch/watchdog.h>
#include <timer.h>
#include <io.h>

static int aml_check(unsigned long pBufferSRC,unsigned long pBufferDST,unsigned int nLength,unsigned int nAESFlag);

void bl2_load_image(void){
	/* dump ddr data when function enabled and flag set */
	dump_ddr_data();

	//meminfo_t *bl2_tzram_layout;
	bl31_params_t *bl2_to_bl31_params;
	entry_point_info_t *bl31_ep_info;
	//meminfo_t bl33_mem_info;
	unsigned int * pCHK = (unsigned int *)FM_FIP_HEADER_LOAD_ADDR;
	unsigned int nAESFlag = 1;
	unsigned int nBL3XLoadAddr = readl(0xc8100228);
	nBL3XLoadAddr = readl(0xc8100228);
	int nSecFlag = nBL3XLoadAddr & (1<<4);
	/*load fip header*/
	aml_fip_header_t *fip_header;
	fip_header = (aml_fip_header_t *)(uint64_t)FM_FIP_HEADER_LOAD_ADDR;
#if defined(CONFIG_AML_SECURE_UBOOT)
	extern  void platform_stack_set_bl2 (unsigned long);
	platform_stack_set_bl2(BL2_SEC_BOOT_SP_BASE);
#endif
	storage_load(BL2_SIZE, (uint64_t)fip_header, sizeof(aml_fip_header_t), "fip header");
	memcpy((void*)FM_FIP_BL3X_TEMP_LOAD_ADDR,(void*)FM_FIP_HEADER_LOAD_ADDR,sizeof(aml_fip_header_t));
	if (TOC_HEADER_NAME == *pCHK && TOC_HEADER_SERIAL_NUMBER == *(pCHK+1))
	{
		nAESFlag = 0;
	}

	aml_check(FM_FIP_BL3X_TEMP_LOAD_ADDR,FM_FIP_HEADER_LOAD_ADDR,sizeof(aml_fip_header_t),nAESFlag);

	/*load and process bl30*/
	image_info_t bl30_image_info;
	entry_point_info_t bl30_ep_info;
	nBL3XLoadAddr = nSecFlag ?  FM_FIP_BL3X_TEMP_LOAD_ADDR : FM_BL30_LOAD_ADDR;
	storage_load(BL2_SIZE + (fip_header->bl30_entry.offset),nBL3XLoadAddr, (fip_header->bl30_entry.size), "bl30");
	parse_blx(&bl30_image_info, &bl30_ep_info,nBL3XLoadAddr, FM_BL30_LOAD_ADDR, (fip_header->bl30_entry.size),nAESFlag);
	/*process bl30*/
	process_bl30x(&bl30_image_info, &bl30_ep_info, "bl30");

#if (NEED_BL301)
	/*load and process bl301*/
	image_info_t bl301_image_info;
	entry_point_info_t bl301_ep_info;
	nBL3XLoadAddr = nSecFlag ?  FM_FIP_BL3X_TEMP_LOAD_ADDR : FM_BL301_LOAD_ADDR;
	storage_load(BL2_SIZE + (fip_header->bl301_entry.offset),nBL3XLoadAddr, (fip_header->bl301_entry.size), "bl301");
	parse_blx(&bl301_image_info, &bl301_ep_info, nBL3XLoadAddr ,FM_BL301_LOAD_ADDR, (fip_header->bl301_entry.size),nAESFlag);
	/*process bl301*/
	process_bl30x(&bl301_image_info, &bl301_ep_info, "bl301");
#endif

	/*load and process bl31*/
	bl2_to_bl31_params = bl2_plat_get_bl31_params();
	bl31_ep_info = bl2_plat_get_bl31_ep_info();
	/* Set the X0 parameter to bl31 */
	bl31_ep_info->args.arg0 = (unsigned long)bl2_to_bl31_params;
	nBL3XLoadAddr = nSecFlag ?  FM_FIP_BL3X_TEMP_LOAD_ADDR : FM_BL31_LOAD_ADDR;
	storage_load(BL2_SIZE + (fip_header->bl31_entry.offset), nBL3XLoadAddr, (fip_header->bl31_entry.size), "bl31");
	parse_blx(bl2_to_bl31_params->bl31_image_info, bl31_ep_info,nBL3XLoadAddr, FM_BL31_LOAD_ADDR, (fip_header->bl31_entry.size),nAESFlag);
	bl2_plat_set_bl31_ep_info(bl2_to_bl31_params->bl31_image_info, bl31_ep_info);

#if (NEED_BL32)
	/*
	 * Load the BL32 image if there's one. It is upto to platform
	 * to specify where BL32 should be loaded if it exists. It
	 * could create space in the secure sram or point to a
	 * completely different memory.
	 *
	 * If a platform does not want to attempt to load BL3-2 image
	 * it must leave NEED_BL32=0
	 */
	meminfo_t bl32_mem_info;
	bl2_plat_get_bl32_meminfo(&bl32_mem_info);
	nBL3XLoadAddr = nSecFlag ?  FM_FIP_BL3X_TEMP_LOAD_ADDR : FM_BL32_LOAD_ADDR;
	storage_load(BL2_SIZE + fip_header->bl32_entry.offset, nBL3XLoadAddr , fip_header->bl32_entry.size, "bl32");
	parse_blx(bl2_to_bl31_params->bl32_image_info, bl2_to_bl31_params->bl32_ep_info,
				nBL3XLoadAddr, FM_BL32_LOAD_ADDR, fip_header->bl32_entry.size,nAESFlag);
	bl2_plat_set_bl32_ep_info(bl2_to_bl31_params->bl32_image_info, bl2_to_bl31_params->bl32_ep_info);
#endif /* NEED_BL32 */

	/*load and process bl33*/
	nBL3XLoadAddr = nSecFlag ?  FM_FIP_BL3X_TEMP_LOAD_ADDR : FM_BL33_LOAD_ADDR;
	storage_load(BL2_SIZE + fip_header->bl33_entry.offset,nBL3XLoadAddr, fip_header->bl33_entry.size, "bl33");
	parse_blx(bl2_to_bl31_params->bl33_image_info, bl2_to_bl31_params->bl33_ep_info,
				nBL3XLoadAddr, FM_BL33_LOAD_ADDR, fip_header->bl33_entry.size,nAESFlag);
	//bl2_plat_get_bl33_meminfo(&bl33_mem_info);
	bl2_plat_set_bl33_ep_info(bl2_to_bl31_params->bl33_image_info, bl2_to_bl31_params->bl33_ep_info);

	/* Flush the params to be passed to memory */
	bl2_plat_flush_bl31_params();

	/*disable mmu and dcache, flush dcache, then enter next firmware*/
	disable_mmu_el1();
	watchdog_disable();
	/*
	 * Run BL31 via an SMC to BL1. Information on how to pass control to
	 * the BL32 (if present) and BL33 software images will be passed to
	 * BL31 as an argument.
	 */

	smc(RUN_IMAGE, (unsigned long)bl31_ep_info, 0, 0, 0, 0, 0, 0);

/*
	typedef unsigned long (*FUNC_TPL)(void );
	unsigned long bl33_entry = FM_BL33_LOAD_ADDR;
	serial_puts("bl33 entry: 0x");
	serial_put_hex(bl33_entry, 32);
	serial_puts("\n");
	FUNC_TPL func_tpl=(FUNC_TPL)bl33_entry;
	func_tpl();
*/
}

static int aml_check(unsigned long pBufferSRC,unsigned long pBufferDST,unsigned int nLength,unsigned int nAESFlag)
{
	int nReturn = aml_data_check(pBufferSRC,pBufferDST,nLength,nAESFlag);
	if (nReturn)
	{
		//printf("aml log : SIG CHK : %d for address 0x%08X\n",nReturn,pBufferSRC);
		serial_puts("aml log : SIG CHK : ");
		serial_put_dec(nReturn);
		serial_puts(" for address 0x");
		serial_put_hex(pBufferSRC, 32);
		serial_puts("\n");
		//while(1);
		check_handler();
	}

	return nReturn;
}

/*blx header parse function*/
void parse_blx(image_info_t *image_data,
				entry_point_info_t *entry_point_info,
				unsigned int src,
				unsigned int dst,
				unsigned int length,
				unsigned int nAESFlag)
{
	image_data->image_base = dst;
	image_data->image_size = length;
	if (entry_point_info != NULL)
		entry_point_info->pc = dst;

	aml_check(src,dst,length,nAESFlag);
}

/*process bl30x, transfer to m3, etc..*/
void process_bl30x(image_info_t *image_data,
				entry_point_info_t *entry_point_info, const char * name)
{
	//serial_puts("start sha2\n");
	uint8_t _sha2[32] = {0};
	sha2((const uint8_t *)image_data->image_base,
		image_data->image_size,
		_sha2,
		0); /*0 means sha256, else means sha224*/

#if 0
	serial_puts(name);
	serial_puts(" sha2:");
	int print_loop = 0;
	for (print_loop=0; print_loop<32; print_loop++) {
		if (0 == (print_loop % 16))
			serial_puts("\n");
		serial_put_hex(_sha2[print_loop], 8);
		serial_puts(" ");
	}
	serial_puts("\n");
#endif

	send_bl30x(image_data->image_base, image_data->image_size, \
		_sha2, sizeof(_sha2), name);
	return;
}

void bl2_to_romcode(uintptr_t entry)
{
	bl31_params_t *bl2_to_bl31_params;
	entry_point_info_t *bl31_ep_info;

	bl2_to_bl31_params = bl2_plat_get_bl31_params();
	bl31_ep_info = bl2_plat_get_bl31_ep_info();
	/* Set the X0 parameter to bl31 */
	bl31_ep_info->args.arg0 = (unsigned long)bl2_to_bl31_params;

	bl31_ep_info->pc = entry;
	SET_SECURITY_STATE(bl31_ep_info->h.attr, SECURE);
	bl31_ep_info->spsr = SPSR_64(MODE_EL3, MODE_SP_ELX,
				       (DAIF_FIQ_BIT | DAIF_IRQ_BIT | DAIF_DBG_BIT));

	watchdog_disable();
	disable_mmu_el1();

	bl31_ep_info->args.arg0 = 0;
	smc(RUN_IMAGE, (unsigned long)bl31_ep_info, 0, 0, 0, 0, 0, 0);
}

void check_handler(void) {
	if (BOOT_DEVICE_USB == get_boot_device()) {
		serial_puts("USB mode!\n");
		bl2_to_romcode(USB_BL2_RETURN_ROM_ADDR);
	}
	else{
		serial_puts("reset...\n");
		reset_system();
	}
}
