/**
 * @file zmodDAC1411.cpp
 * @author Cristian Fatu
 * @date 15 Nov 2019
 * @brief File containing implementations of the ZMOD DAC1411 specific methods.
 */

#include <stdio.h>
#include <unistd.h>

#include "zmoddac1411.h"
#include <stdlib.h>
/**
 * Initialize a ZMOD DAC1411 instance.
 *
 * @param baseAddr the base address of the ZMOD device,
 *  can be found by either looking in the device tree files on a Linux
 *  platform, or the xparameters.h file on a baremetal platform,
 *  or in Vivado's Address Editor tab.
 * @param dmaAddr the base address of the DMA device,
 *  can be found by either looking in the device tree files on a Linux
 *  platform, or the xparameters.h file on a baremetal platform,
 *  or in Vivado's Address Editor tab.
 * @param iicAddress the base address of the I2C device used for flash,
 *  can be found by either looking in the device tree files on a Linux
 *  platform, or the xparameters.h file on a baremetal platform,
 *  or in Vivado's Address Editor tab.
 * @param flashAddress is the I2C slave address of the I2C device used for flash,
 *  can be found in the carrier board reference manual, associated to the SZG connector
 *  where the ZMpd is plugged,
 * @param direction the direction of the DMA transfer,
 *  can be either DMA_DIRECTION_TX for a transfer from the processor
 *  to the FPGA, or DMA_DIRECTION_RX for a transfer from the FPGA
 *  to the processor.
 * @param zmodInterrupt the interrupt number of the ZMOD device,
 *  can be found by looking at the xparameters.h file on a baremental
 *  platform, and is irrelevant on a Linux platform.
 * @param dmaInterrupt the interrupt number of the DMA device,
 *  can be found by looking at the xparameters.h file on a baremental
 *  platform, and is irrelevant on a Linux platform.
 */
ZMODDAC1411::ZMODDAC1411(uintptr_t baseAddress, uintptr_t dmaAddress, uintptr_t iicAddress, uintptr_t flashAddress, int dmaInterrupt)
		: ZMOD(baseAddress, dmaAddress, iicAddress, flashAddress, DMA_DIRECTION_TX, -1, dmaInterrupt)
{
	ZMOD::initCalib(sizeof(CALIBECLYPSEDAC), ZMODDAC1411_CALIB_ID, ZMODDAC1411_CALIB_USER_ADDR, ZMODDAC1411_CALIB_FACT_ADDR);
}


/**
* Allocates the data buffer used for AXI DMA transfers, 4 bytes for each sample.
*
* @param length the number of samples in the buffer.
*
* @return the pointer to the allocated buffer
*
*/
uint32_t* ZMODDAC1411::allocChannelsBuffer(size_t length) {
	return (uint32_t *)ZMOD::allocDMABuffer(length * sizeof(uint32_t));
}

/**
* Free the data buffer used for AXI DMA transfers, 4 bytes for each sample.
*
 * @param buf the address of the DMA buffer
* @param length the number of samples in the buffer.
*
*
*/
void ZMODDAC1411::freeChannelsBuffer(uint32_t *buf, size_t length) {
	ZMOD::freeDMABuffer(buf, length * sizeof(uint32_t));
}

/**
 * Position the channel data in a 32 bits value to be sent to IP.
 *
 * @param channel the channel to extract  0 for channel A, 1 for channel B
 * @param data the unsigned channel data (14 bits)
 *
 * @return the 32 bits value containing the channel data on the proper position
 *
 */
uint32_t ZMODDAC1411::arrangeChannelData(uint8_t channel, uint16_t data)
{
	data &= 0x3FFF; // mask out any bits outside the expected 14 bits
	return (channel ? ((uint32_t)data) << 2 : ((uint32_t)data) << 18);
}

/**
 * Position the channel data in a 32 bits value to be sent to IP.
 *
 * @param channel the channel to extract  0 for channel A, 1 for channel B
 * @param data the signed channel data (14 bits)
 *
 * @return the 32 bits value containing the channel data on the proper position
 *
 */
uint32_t ZMODDAC1411::arrangeSignedChannelData(uint8_t channel, int16_t data)
{
	return arrangeChannelData(channel, (uint32_t)data);
}

/**
 * Reset the output counter so that the next time the instrument is started the first value from the buffer will be sent to DAC.
 *
 */
void ZMODDAC1411::resetOutputCounter()
{
	writeRegFld(ZMODDAC1411_REGFLD_CR_OUT_ADDR_CNT_RST, 1);
}



/**
* Send to the IP the data to be generated by DAC.
*
* @param buffer the buffer containing the data to be sent to DAC,
*  must be previously allocated and filled with data.
* @param length the number of values from buffer to be  used.
*
* @return 0 on success, any other number on failure
*/
uint8_t ZMODDAC1411::setData(uint32_t* buffer, size_t length)
{
	uint8_t Status;
	// DMA TX transfer length in number of elements
	// multiply by the size of the data
	setTransferSize(length * sizeof(uint32_t));

	// Start DMA Transfer
	Status = startDMATransfer(buffer);
	if (Status) {
		return ERR_FAIL;
	}
	// Wait for DMA to Complete transfer
	while(!isDMATransferComplete()) {}
	return Status;
}


/**
* Start the DAC to generate the values previously prepared.
*
*/
void ZMODDAC1411::start()
{
	writeRegFld(ZMODDAC1411_REGFLD_CR_DAC_EN, 1);
}

/**
* Stop the DAC.
*
*/
void ZMODDAC1411::stop()
{
	writeRegFld(ZMODDAC1411_REGFLD_CR_DAC_EN, 0);
}

/**
* Set the 14 bits output sample frequency divider.
*
*/
void ZMODDAC1411::setOutputSampleFrequencyDivider(uint16_t val)
{
	writeRegFld(ZMODDAC1411_REGFLD_CR_DIV_RATE, val);
}
/**
 * Call when a ZMOD interrupt occurs.
 */
void ZMODDAC1411::processInterrupt()
{

}

/**
 * Reads the calibration data into the calib class member.
 * It calls the ZMOD At the ZMOD class level to read the user calibration data as an array of bytes into the area
 * pointed by calib base class member.
 * Then the calibration data is interpreted according to CALIBECLYPSEDAC structure.
 * @return the status: ERR_SUCCESS for success, ERR_CALIB_ID for calib ID error,
 *  ERR_CALIB_CRC for CRC error, ERR_FAIL if calibration is not initialized.
 */
int ZMODDAC1411::readUserCalib()
{
	int status;
	CALIBECLYPSEDAC *pCalib;

	// read the user calibration data as an array of bytes, into the area pointed by calib base class member
	status = ZMOD::readUserCalib();
	if(status == ERR_SUCCESS)
	{
		// interpret the calib data as a CALIBECLYPSEDAC data
		pCalib = (CALIBECLYPSEDAC *)calib;

		// fill the calibration related registers
		// float           cal[2][2][2];   // [channel 0:1][low/high gain 0:1][0 multiplicative : 1 additive]
		writeRegFld(ZMODDAC1411_REGFLD_SC1HGMULTCOEF_VAL, computeCoefMult(pCalib->cal[0][1][0], 1));
		writeRegFld(ZMODDAC1411_REGFLD_SC1HGADDCOEF_VAL,   computeCoefAdd(pCalib->cal[0][1][0], pCalib->cal[0][1][1], 1));
		writeRegFld(ZMODDAC1411_REGFLD_SC1LGMULTCOEF_VAL, computeCoefMult(pCalib->cal[0][0][0], 0));
		writeRegFld(ZMODDAC1411_REGFLD_SC1LGADDCOEF_VAL,   computeCoefAdd(pCalib->cal[0][0][0], pCalib->cal[0][0][1], 0));

		writeRegFld(ZMODDAC1411_REGFLD_SC2HGMULTCOEF_VAL, computeCoefMult(pCalib->cal[1][1][0], 1));
		writeRegFld(ZMODDAC1411_REGFLD_SC2HGADDCOEF_VAL,   computeCoefAdd(pCalib->cal[1][1][0], pCalib->cal[1][1][1], 1));
		writeRegFld(ZMODDAC1411_REGFLD_SC2LGMULTCOEF_VAL, computeCoefMult(pCalib->cal[1][0][0], 0));
		writeRegFld(ZMODDAC1411_REGFLD_SC2LGADDCOEF_VAL,   computeCoefAdd(pCalib->cal[1][0][0], pCalib->cal[1][0][1], 0));
	}

	return ERR_SUCCESS;
}

/**
* Set the gain for a channel.
* @param channel the channel: 0 for channel 1, 1 for channel 2
* @param gain the gain : 0 for LOW gain, 1 for HIGH gain
*/
void ZMODDAC1411::setGain(uint8_t channel, uint8_t gain)
{
	if(channel)
	{
		writeRegFld(ZMODDAC1411_REGFLD_TRIG_SC2_HG_LG, gain);
	}
	else
	{
		writeRegFld(ZMODDAC1411_REGFLD_TRIG_SC1_HG_LG, gain);
	}
}

/**
* Set a pair of calibration values for a specific channel and gain into the calib area (interpreted as CALIBECLYPSEDAC).
* In order for this change to be applied to user calibration area from flash, writeUserCalib function must be called.
* @param channel the channel for which calibration values are set: 0 for channel 1, 1 for channel 2
* @param gain the gain for which calibration values are set: 0 for LOW gain, 1 for HIGH gain
* @param valG the gain calibration value to be set
* @param valA the additive calibration value to be set
*/
void ZMODDAC1411::setCalibValues(uint8_t channel, uint8_t gain, float valG, float valA)
{
	CALIBECLYPSEDAC *pCalib;
	if(calib)
	{
		// interpret the calib data as a CALIBECLYPSEDAC data
		pCalib = (CALIBECLYPSEDAC *)calib;
		pCalib->cal[channel][gain][0] = valG;
		pCalib->cal[channel][gain][1] = valA;
	}
}

#define IDEAL_RANGE_DAC_HIGH 5.0
#define IDEAL_RANGE_DAC_LOW 1.25
#define REAL_RANGE_DAC_HIGH 5.32
#define REAL_RANGE_DAC_LOW 1.33

/**
 * Computes the Multiplicative calibration coefficient.
* @param cg - gain coefficient as it is stored in Flash
* @param gain 0 LOW and 1 HIGH
* @return a signed 32 value containing the multiplicative coef. in the 18 lsb bits: bit 17 sign, bit 16-0 the value
*/
int32_t ZMODDAC1411::computeCoefMult(float cg, uint8_t gain)
{
	float fval = (gain ? (IDEAL_RANGE_DAC_HIGH/REAL_RANGE_DAC_HIGH):(IDEAL_RANGE_DAC_LOW/REAL_RANGE_DAC_LOW))/(1 + cg)*(float)(1<<16);
	// extra 14 positions so that the sign is on 31 position instead of 17
	int32_t ival = (int32_t) (fval + 0.5);	// round
	return ival;
}

/**
 * Computes the Additive calibration coefficient.
 * @param ca - add coefficient as it is stored in Flash
 * @param cg - gain coefficient as it is stored in Flash
 * @param gain 0 LOW and 1 HIGH
 * @return a signed 32 value containing the additive coef.  in the 18 lsb bits: bit 17 sign, bit 16-0 the value
 */
int32_t ZMODDAC1411::computeCoefAdd(float ca, float cg, uint8_t gain)
{
	float fval = -ca * (float)(uint32_t)(1<<17) / ((gain ? REAL_RANGE_DAC_HIGH:REAL_RANGE_DAC_LOW) * (1 + cg));
	int32_t ival = (int32_t) (fval + 0.5);	// round
	return ival;
}

/**
 * Converts a value in Volts measure unit into a signed raw value (to be provided to the ZmodDAC1411 IP core).
 * If the value is outside the range corresponding to the specified gain, it is limited to the nearest range limit.
 * @param raw - the signed value as .
 * @param gain 0 LOW and 1 HIGH
 * @return the Volts value.
 */
int32_t ZMODDAC1411::getSignedRawFromVolt(float voltValue, uint8_t gain)
{
	float vMax = gain ? IDEAL_RANGE_DAC_HIGH:IDEAL_RANGE_DAC_LOW;
	int32_t raw;
	if(voltValue >= vMax)
	{
		// max raw value
		raw = (1<<13) - 1;
	}
	else
	{
		if(voltValue < -vMax)
		{
			// min raw value
			raw = 1<<13;
		}
		else
		{
			raw = (int32_t)(voltValue * (1<<13)/vMax);
		}
	}
	return raw;
}
