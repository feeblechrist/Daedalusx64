#define TEST_DISABLE_EEPROM_FUNCS //return PATCH_RET_NOT_PROCESSED;

u32 Patch___osEepStatus()
{
TEST_DISABLE_EEPROM_FUNCS
	// Return status of Eeeprom in OSContStatus struct passed in a1.
	// a0 is the message queue to block on, and is ignored

	//u32 dwMQ         = gGPR[REG_a0]._u32_0;
	u32 ContStatus = gGPR[REG_a1]._u32_0;

	//DBGConsole_Msg(0, "osEepStatus(0x%08x, 0x%08x), ra = 0x%08x", dwMQ, ContStatus, (u32)g_qwGPR[REG_ra]);

	// Set up ContStatus values
	if ( g_ROM.settings.SaveType == SAVE_TYPE_EEP4K || g_ROM.settings.SaveType == SAVE_TYPE_EEP16K )
	{
		u16 type = (g_ROM.settings.SaveType == SAVE_TYPE_EEP4K) ? CONT_EEPROM : CONT_EEP16K;
		Write16Bits(ContStatus + 0, type );			// type
		Write8Bits(ContStatus + 2, 0);				// status
		Write8Bits(ContStatus + 3, 0);				// errno

		gGPR[REG_v0]._u64 = 0;
	}
	else
	{
		Write16Bits(ContStatus + 0, 0);				// type
		Write8Bits(ContStatus + 2, 0);						// status
		Write8Bits(ContStatus + 3, CONT_NO_RESPONSE_ERROR);	// errno
		gGPR[REG_v0]._u64 = CONT_NO_RESPONSE_ERROR;

	}
	return PATCH_RET_JR_RA;
}


u32 Patch_osEepromProbe()
{
TEST_DISABLE_EEPROM_FUNCS
	// Returns 1 on EEPROM detected, 0 on error/no eeprom
	//DBGConsole_Msg(0, "osEepromProbe(), ra = 0x%08x", (u32)g_qwGPR[REG_ra]);
	
	if ( g_ROM.settings.SaveType == SAVE_TYPE_EEP4K )
		gGPR[REG_v0]._u64 = EEPROM_TYPE_4K;
	else if ( g_ROM.settings.SaveType == SAVE_TYPE_EEP16K )
		gGPR[REG_v0]._u64 = EEPROM_TYPE_16K;
	else
		gGPR[REG_v0]._u64 = 0;

	// Side effect From osEepStatus
	//Write32Bits(VAR_ADDRESS(osEepPifThingamy2), 5);

	return PATCH_RET_JR_RA;
}

u32 Patch_osEepromRead()
{
TEST_DISABLE_EEPROM_FUNCS
	// s32 osEepromRead(OSMesgQueue * mq, u8 page, u8 * buf);
	//u32 dwMQ   = gGPR[REG_a0]._u32_0;
	//u32 dwPage = gGPR[REG_a1]._u32_0;
	//u32 dwBuf  = gGPR[REG_a2]._u32_0;


	//DBGConsole_Msg(0, "osEepromRead(0x%08x, 0x%08x, 0x%08x), ra = 0x%08x",
	//	dwMQ, dwPage, dwBuf, (u32)g_qwGPR[REG_ra]);
	
	return PATCH_RET_NOT_PROCESSED;
}


u32 Patch_osEepromLongRead()
{
TEST_DISABLE_EEPROM_FUNCS
	//u32 dwMQ   = gGPR[REG_a0]._u32_0;
	//u32 dwPage = gGPR[REG_a1]._u32_0;
	//u32 dwBuf  = gGPR[REG_a2]._u32_0;
	//u32 dwLen  = gGPR[REG_a3]._u32_0;


	//DBGConsole_Msg(0, "[WosEepromLongRead(0x%08x, %d, 0x%08x, 0x%08x), ra = 0x%08x]",
	//	dwMQ, dwPage, dwBuf, dwLen, (u32)g_qwGPR[REG_ra]);
	
	return PATCH_RET_NOT_PROCESSED;

}

u32 Patch_osEepromWrite()
{
TEST_DISABLE_EEPROM_FUNCS
	// s32 osEepromWrite(OSMesgQueue * mq, u8 page, u8 * buf);
	//u32 dwMQ   = gGPR[REG_a0]._u32_0;
	//u32 dwPage = gGPR[REG_a1]._u32_0;
	//u32 dwBuf  = gGPR[REG_a2]._u32_0;

	//u32 dwA = Read32Bits(dwBuf + 0);
	//u32 dwB = Read32Bits(dwBuf + 4);


	//DBGConsole_Msg(0, "osEepromWrite(0x%08x, %d, [0x%08x] = 0x%08x%08x), ra = 0x%08x",
	//	dwMQ, dwPage, dwBuf, dwA, dwB, (u32)g_qwGPR[REG_ra]);
	
	return PATCH_RET_NOT_PROCESSED;
}

u32 Patch_osEepromLongWrite()
{
TEST_DISABLE_EEPROM_FUNCS
	//u32 dwMQ   = gGPR[REG_a0]._u32_0;
	//u32 dwPage = gGPR[REG_a1]._u32_0;
	//u32 dwBuf  = gGPR[REG_a2]._u32_0;
	//u32 dwLen  = gGPR[REG_a3]._u32_0;


	//DBGConsole_Msg(0, "[WosEepromLongWrite(0x%08x, %d, 0x%08x, 0x%08x), ra = 0x%08x]",
	//	dwMQ, dwPage, dwBuf, dwLen, (u32)g_qwGPR[REG_ra]);


	return PATCH_RET_NOT_PROCESSED;

}
