/*
 * cc1101_oregon.cpp
 *
 *  Created on: 13Apr.,2020
 *  Oregon-specific code by Ivaylo Haratcherev
 *
 '  CC1101 - Oregon Raspberry Pi Library
 '  ------------------------------------
 '
 '
 '  This module contains helper code from other people.
 '  See README for Acknowledgments
 '-----------------------------------------------------------------------------
 */

#include "cc1101_oregon.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wiringPi.h>
#include <wiringPiSPI.h>


static uint8_t cc1101_OOK_Oregon[CFG_REGISTER] = {
                    0x06,  // IOCFG2        GDO2 Output Pin Configuration
                    0x2E,  // IOCFG1        GDO1 Output Pin Configuration
                    0x06,  // IOCFG0        GDO0 Output Pin Configuration
                    0x4f,  // ***FIFOTHR       RX FIFO and TX FIFO Thresholds
                    0xcc,  // ***SYNC1         Sync Word, High Byte
                    0xcc,  // ***SYNC0         Sync Word, Low Byte
                    0x29,  // ***PKTLEN        Packet Length
                    0x04,  // PKTCTRL1      Packet Automation Control
                    0x00,  // ***PKTCTRL0      Packet Automation Control
                    0x00,  // ADDR          Device Address
                    0x00,  // CHANNR        Channel Number
                    0x06,  // FSCTRL1       Frequency Synthesizer Control
                    0x00,  // FSCTRL0       Frequency Synthesizer Control
                    0x10,  // FREQ2         Frequency Control Word, High Byte
                    0xB0,  // FREQ1         Frequency Control Word, Middle Byte
                    0x71,  // FREQ0         Frequency Control Word, Low Byte
                    0xc6,  // MDMCFG4       Modem Configuration
                    0x4A,  // MDMCFG3       Modem Configuration
                    0x37,  // MDMCFG2       Modem Configuration
                    0x22,  // MDMCFG1       Modem Configuration
                    0xF8,  // MDMCFG0       Modem Configuration
                    0x15,  // DEVIATN       Modem Deviation Setting
                    0x07,  // MCSM2         Main Radio Control State Machine Configuration
                    0x30,  // MCSM1         Main Radio Control State Machine Configuration
                    0x18,  // MCSM0         Main Radio Control State Machine Configuration
                    0x16,  // FOCCFG        Frequency Offset Compensation Configuration
                    0x6C,  // BSCFG         Bit Synchronization Configuration
                    0x07,  // AGCCTRL2      AGC Control // 0x07
                    0x00,  // AGCCTRL1      AGC Control  // 0x00
                    0x91,  // AGCCTRL0      AGC Control  // 0x91
                    0x87,  // WOREVT1       High Byte Event0 Timeout
                    0x6B,  // WOREVT0       Low Byte Event0 Timeout
                    0xFB,  // WORCTRL       Wake On Radio Control
                    0x56,  // FREND1        Front End RX Configuration
                    0x11,  // FREND0        Front End TX Configuration
                    0xE9,  // FSCAL3        Frequency Synthesizer Calibration
                    0x2A,  // FSCAL2        Frequency Synthesizer Calibration
                    0x00,  // FSCAL1        Frequency Synthesizer Calibration
                    0x1F,  // FSCAL0        Frequency Synthesizer Calibration
                    0x41,  // RCCTRL1       RC Oscillator Configuration
                    0x00,  // RCCTRL0       RC Oscillator Configuration
                    0x59,  // FSTEST        Frequency Synthesizer Calibration Control
                    0x7F,  // PTEST         Production Test
                    0x3F,  // AGCTEST       AGC Test
                    0x81,  // TEST2         Various Test Settings
                    0x35,  // TEST1         Various Test Settings
                    0x09,  // TEST0         Various Test Settings
               };


               //Patable index: -30  -20- -15  -10   0    5    7    10 dBm
static uint8_t patable_power_433[8] = {0x6C,0x1C,0x06,0x3A,0x51,0x85,0xC8,0xC0};

//----------------------------------[END]---------------------------------------

//-------------------------[CC1101 reset function]------------------------------
void CC1101_Oregon::reset(void)                  // reset defined in cc1101 datasheet
{
    digitalWrite(SS_PIN, LOW);
    delayMicroseconds(10);
    digitalWrite(SS_PIN, HIGH);
    delayMicroseconds(40);

    spi_write_strobe(SRES);
    delay(1);
}
//-----------------------------[END]--------------------------------------------

//------------------------[set Power Down]--------------------------------------
void CC1101_Oregon::powerdown(void)
{
    sidle();
    spi_write_strobe(SPWD);               // CC1101 Power Down
}
//-----------------------------[end]--------------------------------------------

//---------------------------[WakeUp]-------------------------------------------
void CC1101_Oregon::wakeup(void)
{
    digitalWrite(SS_PIN, LOW);
    delayMicroseconds(10);
    digitalWrite(SS_PIN, HIGH);
    delayMicroseconds(10);
    receive();                            // go to RX Mode
}
//-----------------------------[end]--------------------------------------------

//---------------------[CC1101 set debug level]---------------------------------
uint8_t CC1101_Oregon::set_debug_level(uint8_t set_debug_level)  //default ON
{
    debug_level = set_debug_level;        //set debug level of CC1101 outputs

    return debug_level;
}
//-----------------------------[end]--------------------------------------------

//---------------------[CC1101 get debug level]---------------------------------
uint8_t CC1101_Oregon::get_debug_level(void)
{
    return debug_level;
}
//-----------------------------[end]--------------------------------------------

//----------------------[CC1101 init functions]---------------------------------
uint8_t CC1101_Oregon::begin(uint8_t debug_level)
{
    uint8_t partnum, version;

//    pinMode(GDO0, INPUT);                 //setup AVR GPIO ports
    pinMode(GDO2, INPUT);

    set_debug_level(debug_level);   //set debug level of CC1101 outputs

    if(debug_level > 0){
          printf("Init CC1101...\r\n");
    }

    spi_begin();                          //inits SPI Interface
    reset();                              //CC1101 init reset

    spi_write_strobe(SFTX);delayMicroseconds(100);//flush the TX_fifo content
    spi_write_strobe(SFRX);delayMicroseconds(100);//flush the RX_fifo content

    partnum = spi_read_register(PARTNUM); //reads CC1101 partnumber
    version = spi_read_register(HW_VERSION); //reads CC1101 version number

    //checks if valid Chip ID is found. Usualy 0x03 or 0x14. if not -> abort
    if(version == 0x00 || version == 0xFF){
        if(debug_level > 0){
            printf("no CC11xx found!\r\n");
        }
        return FALSE;
    }

    if(debug_level > 0){
          printf("Partnumber: 0x%02X\r\n", partnum);
          printf("Version   : 0x%02X\r\n", version);
    }


    //set modulation mode
    spi_write_burst(WRITE_BURST,cc1101_OOK_Oregon,CFG_REGISTER);

    //set PA table (is this needed in Rx only mode?)
    spi_write_burst(PATABLE_BURST,patable_power_433,8);

    if(debug_level > 0){
          printf("...done!\r\n");
    }

    receive();                                  //set CC1101 in receive mode

    return TRUE;
}
//-------------------------------[end]------------------------------------------

//-----------------[finish's the CC1101 operation]------------------------------
void CC1101_Oregon::end(void)
{
    powerdown();                          //power down CC1101
}
//-------------------------------[end]------------------------------------------

//-----------------------[show all CC1101 registers]----------------------------
void CC1101_Oregon::show_register_settings(void)
{
	uint8_t config_reg_verify[CFG_REGISTER],Patable_verify[CFG_REGISTER];

	spi_read_burst(READ_BURST,config_reg_verify,CFG_REGISTER);  //reads all 47 config register from cc1101
	spi_read_burst(PATABLE_BURST,Patable_verify,8);             //reads output power settings from cc1101

	//show_main_settings();
	printf("Config Register:\r\n");

	for(uint8_t i = 0 ; i < CFG_REGISTER; i++)  //showes rx_buffer for debug
	{
		printf("0x%02X ", config_reg_verify[i]);
		if(i==9 || i==19 || i==29 || i==39) //just for beautiful output style
		{
			printf("\r\n");
		}
	}
	printf("\r\n");
	printf("PaTable:\r\n");

	for(uint8_t i = 0 ; i < 8; i++)         //showes rx_buffer for debug
	{
		printf("0x%02X ", Patable_verify[i]);
	}
	printf("\r\n");
}
//-------------------------------[end]------------------------------------------

//--------------------------[show settings]-------------------------------------
void CC1101_Oregon::show_main_settings(void)
{
	// based on register settings in cc1101_OOK_Oregon above
    printf("Mode: ASK/OOK Oregon-specific (no HW manchester)\r\n");
    printf("Frequency: 433.92 MHz\r\n");
    printf("RF Channel: 0\r\n");
}
//-------------------------------[end]------------------------------------------

//----------------------------[idle mode]---------------------------------------
uint8_t CC1101_Oregon::sidle(void)
{
    uint8_t marcstate;

    spi_write_strobe(SIDLE);              //sets to idle first. must be in

    marcstate = 0xFF;                     //set unknown/dummy state value

    while(marcstate != 0x01)              //0x01 = sidle
    {
        marcstate = (spi_read_register(MARCSTATE) & 0x1F); //read out state of cc1101 to be sure in RX
    }
    delayMicroseconds(100);
    return TRUE;
}
//-------------------------------[end]------------------------------------------


//---------------------------[receive mode]-------------------------------------
uint8_t CC1101_Oregon::receive(void)
{
    uint8_t marcstate;

    sidle();                              //sets to idle first.
    spi_write_strobe(SRX);                //writes receive strobe (receive mode)

    marcstate = 0xFF;                     //set unknown/dummy state value

    while(marcstate != 0x0D)              //0x0D = RX
    {
        marcstate = (spi_read_register(MARCSTATE) & 0x1F); //read out state of cc1101 to be sure in RX
    }
    delayMicroseconds(100);
    return TRUE;
}
//-------------------------------[end]------------------------------------------

//------------[enables WOR Mode  EVENT0 ~1890ms; rx_timeout ~235ms]--------------------
void CC1101_Oregon::wor_enable()
{
/*
    EVENT1 = WORCTRL[6:4] -> Datasheet page 88
    EVENT0 = (750/Xtal)*(WOREVT1<<8+WOREVT0)*2^(5*WOR_RES) = (750/26Meg)*65407*2^(5*0) = 1.89s

                        (WOR_RES=0;RX_TIME=0)               -> Datasheet page 80
i.E RX_TIMEOUT = EVENT0*       (3.6038)      *26/26Meg = 235.8ms
                        (WOR_RES=0;RX_TIME=1)               -> Datasheet page 80
i.E.RX_TIMEOUT = EVENT0*       (1.8029)      *26/26Meg = 117.9ms
*/
    sidle();

    spi_write_register(MCSM0, 0x18);    //FS Autocalibration
    spi_write_register(MCSM2, 0x01);    //MCSM2.RX_TIME = 1b

    // configure EVENT0 time
    spi_write_register(WOREVT1, 0xFF);  //High byte Event0 timeout
    spi_write_register(WOREVT0, 0x7F);  //Low byte Event0 timeout

    // configure EVENT1 time
    spi_write_register(WORCTRL, 0x78);  //WOR_RES=0b; tEVENT1=0111b=48d -> 48*(750/26MHz)= 1.385ms

    spi_write_strobe(SFRX);             //flush RX buffer
    spi_write_strobe(SWORRST);          //resets the WOR timer to the programmed Event 1
    spi_write_strobe(SWOR);             //put the radio in WOR mode when CSn is released

    delayMicroseconds(100);
}
//-------------------------------[end]------------------------------------------

//------------------------[disable WOR Mode]-------------------------------------
void CC1101_Oregon::wor_disable()
{
    sidle();                            //exit WOR Mode
    spi_write_register(MCSM2, 0x07);    //stay in RX. No RX timeout
}
//-------------------------------[end]------------------------------------------

//------------------------[resets WOR Timer]------------------------------------
void CC1101_Oregon::wor_reset()
{
    sidle();                            //go to IDLE
    spi_write_register(MCSM2, 0x01);    //MCSM2.RX_TIME = 1b
    spi_write_strobe(SFRX);             //flush RX buffer
    spi_write_strobe(SWORRST);          //resets the WOR timer to the programmed Event 1
    spi_write_strobe(SWOR);             //put the radio in WOR mode when CSn is released

    delayMicroseconds(100);
}
//-------------------------------[end]------------------------------------------

//-------------------------[tx_payload_burst]-----------------------------------
uint8_t CC1101_Oregon::tx_payload_burst(uint8_t my_addr, uint8_t rx_addr,
                              uint8_t *txbuffer, uint8_t length)
{
    txbuffer[0] = length-1;
    txbuffer[1] = rx_addr;
    txbuffer[2] = my_addr;

    spi_write_burst(TXFIFO_BURST,txbuffer,length); //writes TX_Buffer +1 because of pktlen must be also transfered

    if(debug_level > 0){
        printf("TX_FIFO: ");
        for(uint8_t i = 0 ; i < length; i++)       //TX_fifo debug out
        {
             printf("0x%02X ", txbuffer[i]);
        }
        printf("\r\n");
  }
  return TRUE;
}
//-------------------------------[end]------------------------------------------

//------------------[rx_payload_burst - package received]-----------------------
uint8_t CC1101_Oregon::rx_payload_burst(uint8_t rxbuffer[], uint8_t &pktlen)
{
    uint8_t bytes_in_RXFIFO = 0;
    uint8_t res = 0;

    bytes_in_RXFIFO = spi_read_register(RXBYTES);              //reads the number of bytes in RXFIFO

    if((bytes_in_RXFIFO & 0x7F) && !(bytes_in_RXFIFO & 0x80))  //if bytes in buffer and no RX Overflow
    {
        spi_read_burst(RXFIFO_BURST, rxbuffer, bytes_in_RXFIFO);
        pktlen = bytes_in_RXFIFO;
        res = TRUE;
    }
    else
    {
        res = FALSE;
    }
    sidle();                                                  //set to IDLE
    spi_write_strobe(SFRX);delayMicroseconds(100);            //flush RX Buffer
    receive();                                                //set to receive mode

    return res;
}
//-------------------------------[end]------------------------------------------


//----------------------[check if Packet is received]---------------------------
uint8_t CC1101_Oregon::packet_available()
{
    if (digitalRead(GDO2) == TRUE)                           //if RF package received
    {
       while (digitalRead(GDO2) == TRUE) ;               //wait till sync word is fully received
       return TRUE;
    }
    return FALSE;
}
//-------------------------------[end]------------------------------------------


uint8_t CC1101_Oregon::oregon_decode(uint8_t rxbuffer[], uint8_t pos, uint8_t &pktlen, uint8_t offset_bits)
{
	uint16_t curr_window;
	uint32_t curr_window32;
	uint8_t i,j;
	uint8_t *rxbuffer_loc = rxbuffer+pos;
	// shift buffer with offset_bits
    for(i = 0 ; i < pktlen-1; i++)
    {
    	curr_window = (rxbuffer_loc[i] << 8) + rxbuffer_loc[i+1];
    	rxbuffer_loc[i] = ((curr_window >> (8-offset_bits)) & 0xFF);
 	    if (rxbuffer_loc[i] != 0x99 && rxbuffer_loc[i] != 0x96 && rxbuffer_loc[i] != 0x69 && rxbuffer_loc[i] != 0x66) {
 	    	if (debug_level > 0)
 			   printf("Oregon packet bit error!\n");
 	    	return FALSE;
 	    }
    }
    if (rxbuffer_loc[0] != 0x96 || rxbuffer_loc[1] != 0x96) {
    	if (debug_level > 0)
		   printf("Oregon sync nibble (0xA) not found!\n");
		return FALSE;
    }
	rxbuffer_loc += 2;
	pktlen -= 3;
    // do manchester and double-bit decode simultaneously,
    // result goes in the same buffer, pktlen updated accordingly
	pktlen /= 4;
    for(i = 0 ; i < pktlen; i++)
    {
    	curr_window32 = (rxbuffer_loc[i*4] << 8) + rxbuffer_loc[i*4+1] + (rxbuffer_loc[i*4+2] << 24) + (rxbuffer_loc[i*4+3] << 16);
    	rxbuffer[i] = 0;
    	for (j=0; j < 8; j++) {
    		rxbuffer[i] = (rxbuffer[i] << 1) + (((curr_window32 & 0xf)==0x6)?1:0);
    		curr_window32 >>=  4;
    	}
    }
	return TRUE;
}



//------------------[check Payload for ACK or Data]-----------------------------
uint8_t CC1101_Oregon::get_oregon_raw(uint8_t rxbuffer[], uint8_t &pktlen, int8_t &rssi_dbm, uint8_t &lqi)
{
    uint8_t i, res;
    int8_t offset_bits;

    rx_fifo_erase(rxbuffer);                               //delete rx_fifo bufffer

    if(rx_payload_burst(rxbuffer, pktlen) == FALSE)        //read package in buffer
    {
        rx_fifo_erase(rxbuffer);                           //delete rx_fifo bufffer
        return FALSE;                                    //exit
    }
    else
    {
    	if (pktlen<32) {
        	if (debug_level > 0)
        		printf("Packet number less than 32!\n");
    		return FALSE;
    	}
            rssi_dbm = rssi_convert(rxbuffer[pktlen-2]); //converts receiver strength to dBm
            lqi = lqi_convert(rxbuffer[pktlen-1]);       //get rf quialtiy indicator
            pktlen -= 2; //compensate for rssi and lqi

            if(debug_level > 1) {                           //debug output messages
                printf("RX_FIFO: ");
                for(i = 0 ; i < pktlen; i++)   //shows rx_buffer for debug
                {
                		printf("%02X ", rxbuffer[i]);
                }
                printf("\r\n");

                printf("RSSI: %d  ", rssi_dbm);
                printf("LQI: %d ", lqi);
                printf("\r\n");
            }
#if THN122N_CHECK_CC_IN_BUF
			// check if first 2 bytes are CC
			if (rxbuffer[0] != 0xCC || rxbuffer[1] != 0xCC )
			{
				if (debug_level > 0)
					printf("First 2 bytes are not 0xCC!\n");
				return FALSE;
			}
#endif
			// determine sync nibble start offset
            for(i = THN122N_START_SEARCH_AT ; i < 5; i++)
            {
            		if (rxbuffer[i] == 0xD2 || rxbuffer[i] == 0xCD) {
            			if (rxbuffer[i] == 0xD2)
            				offset_bits = 3;
            			else
            				offset_bits = 7;
            			pktlen -= i; // compensate for sync start
            			break;
            		}
            }
            if (i==5)
			{
            	if (debug_level > 0)
            		printf("Start of Oregon sync nibble not found!\n");
            	return FALSE;
			}
            if(debug_level > 1) {                           //debug output messages
            	printf("sync @ pos %d, offset %d\n", i, offset_bits);
            }
            // oregon decode with an offset
            res = oregon_decode(rxbuffer, i, pktlen, offset_bits);

            return res;
    }
}

//-------------------------------[end]------------------------------------------

uint8_t CC1101_Oregon::get_oregon_data(uint8_t rxbuffer[], uint8_t pktlen, oregon_data_t *oregon_data)
{
    uint8_t chn;
    uint16_t checksum;
	double temperature;
	uint8_t i,j;

	// for the moment decoding only THN122N/THN132N sensors!
    if (rxbuffer[0] != 0xEC || rxbuffer[1] != 0x40) {
		printf("Oregon ID 0xEC40 not found!\n");
		return FALSE;
    }
    oregon_data->sensor_id = (rxbuffer[0] << 8) + rxbuffer[1];
    // calculate checksum
    checksum = 0;
    for(i = 0 ; i < 6; i++)
		for (j=0; j < 2; j++) {
			checksum += (rxbuffer[i] >> (j*4)) & 0xf;
		}
    // if checksum larger than 0xff, add upper byte bits to lower byte
    checksum = ((checksum & 0xff) + (checksum >> 8)) & 0xff;
    // invert checksum nibbles
    checksum = (checksum >> 4) + ((checksum << 4) & 0xf0);
    oregon_data->cksum_ok = (checksum == rxbuffer[6]);
    oregon_data->batt_low = (rxbuffer[3] & 0x4) >> 2;
    temperature = 0;
    temperature = ((rxbuffer[5] & 0xf0) >> 4) * 10 + (rxbuffer[4] & 0xf) + ((rxbuffer[4] & 0xf0) >> 4) * 0.1;
    if (rxbuffer[5] & 0xf)
    	temperature = -temperature;
    oregon_data->temperature = temperature;
    chn = (rxbuffer[2] & 0xf0) >> 4;
    for(i = 1 ; i < 5; i++) {
    	chn >>= 1;
    	if (!chn)
    		break;
    }
    oregon_data->channel = i;
    oregon_data->roll_code = ((rxbuffer[2] & 0xf) << 4) + ((rxbuffer[3] & 0xf0) >> 4);
    return TRUE;
}



//--------------------------[tx_fifo_erase]-------------------------------------
void CC1101_Oregon::tx_fifo_erase(uint8_t *txbuffer)
{
    memset(txbuffer, 0, sizeof(FIFOBUFFER));  //erased the TX_fifo array content to "0"
}
//-------------------------------[end]------------------------------------------

//--------------------------[rx_fifo_erase]-------------------------------------
void CC1101_Oregon::rx_fifo_erase(uint8_t *rxbuffer)
{
    memset(rxbuffer, 0, sizeof(FIFOBUFFER)); //erased the RX_fifo array content to "0"
}
//-------------------------------[end]------------------------------------------



//--------------------------[set PA table]-------------------------------------
void CC1101_Oregon::set_patable(uint8_t *patable_arr)
{
    spi_write_burst(PATABLE_BURST,patable_arr,8);   //writes output power settings to cc1101    "104us"
}
//-------------------------------[end]------------------------------------------


//--------------------------[rssi_convert]--------------------------------------
int8_t CC1101_Oregon::rssi_convert(uint8_t Rssi_hex)
{
    int8_t rssi_dbm;
    int16_t Rssi_dec;

    Rssi_dec = Rssi_hex;        //convert unsigned to signed

    if(Rssi_dec >= 128){
        rssi_dbm=((Rssi_dec-256)/2)-RSSI_OFFSET_868MHZ;
    }
    else{
        if(Rssi_dec<128){
            rssi_dbm=((Rssi_dec)/2)-RSSI_OFFSET_868MHZ;
        }
    }
    return rssi_dbm;
}
//-------------------------------[end]------------------------------------------

//----------------------------[lqi convert]-------------------------------------
uint8_t CC1101_Oregon::lqi_convert(uint8_t lqi)
{
    return (lqi & 0x7F);
}
//-------------------------------[end]------------------------------------------

//----------------------------[check crc]---------------------------------------
uint8_t CC1101_Oregon::check_crc(uint8_t lqi)
{
    return (lqi & 0x80);
}
//-------------------------------[end]------------------------------------------

//|==================== SPI Initialization for CC1101 =========================|
void CC1101_Oregon::spi_begin(void)
{
     int x = 0;
     //printf ("init SPI bus... ");
     if ((x = wiringPiSPISetup (0, 8000000)) < 0)  //4MHz SPI speed
     {
          if(debug_level > 0){
          printf ("ERROR: wiringPiSPISetup failed!\r\n");
          }
     }
}
//------------------[write register]--------------------------------
void CC1101_Oregon::spi_write_register(uint8_t spi_instr, uint8_t value)
{
     uint8_t tbuf[2] = {0};
     tbuf[0] = spi_instr | WRITE_SINGLE_BYTE;
     tbuf[1] = value;
     uint8_t len = 2;
     wiringPiSPIDataRW (0, tbuf, len) ;

     return;
}
//|============================ read register ============================|
uint8_t CC1101_Oregon::spi_read_register(uint8_t spi_instr)
{
     uint8_t value;
     uint8_t rbuf[2] = {0};
     rbuf[0] = spi_instr | READ_SINGLE_BYTE;
     uint8_t len = 2;
     wiringPiSPIDataRW (0, rbuf, len) ;
     value = rbuf[1];
     return value;
}
//|========================= write a command ========================|
void CC1101_Oregon::spi_write_strobe(uint8_t spi_instr)
{
     uint8_t tbuf[1] = {0};
     tbuf[0] = spi_instr;
     wiringPiSPIDataRW (0, tbuf, 1) ;
 }
//|======= read multiple registers =======|
void CC1101_Oregon::spi_read_burst(uint8_t spi_instr, uint8_t *pArr, uint8_t len)
{
     uint8_t rbuf[len + 1];
     rbuf[0] = spi_instr | READ_BURST;
     wiringPiSPIDataRW (0, rbuf, len + 1) ;
     for (uint8_t i=0; i<len ;i++ )
     {
          pArr[i] = rbuf[i+1];
     }
}
//|======= write multiple registers =======|
void CC1101_Oregon::spi_write_burst(uint8_t spi_instr, uint8_t *pArr, uint8_t len)
{
     uint8_t tbuf[len + 1];
     tbuf[0] = spi_instr | WRITE_BURST;
     for (uint8_t i=0; i<len ;i++ )
     {
          tbuf[i+1] = pArr[i];
     }
     wiringPiSPIDataRW (0, tbuf, len + 1) ;
}
//|================================= END =======================================|



