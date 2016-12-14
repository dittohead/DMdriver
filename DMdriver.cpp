/*
 * This is the main file of the Arduino library for DM63x LED driver chips
 * (c) 2016 Dmitry Reznkov, dmitry@ultiblink.com
 * The library is free software under GNU General Public License.
 * You can use it any way you wish, but NO WARRANTY is provided.
 *
 * Special thanks: Alex Leone and his excellent Arduino TLC5940 library
 * that served as an inspiration for this one.
*/
#include "Arduino.h"
#include "DMdriver.h"

DMdriver::DMdriver(uint8_t Driver, uint8_t Number, uint8_t Latch)
{
	DMnum = Number; // number of DM chips in chain
	DMtype = Driver; // 16 or 12 bits (32 or 24 bytes per chip)
	LATpin = Latch; // latch pin
	pinNum = DMnum * 16; // number of outputs (single color LEDs)
#ifdef DM256PWM
	byteNum = pinNum; // if 8bit resolution was defined, 16 bytes per chip used
#else
	byteNum = DMnum * DMtype;
#endif
	
}

void DMdriver::dm_shift(uint8_t value)
{
    SPDR = value; 
    while (!(SPSR & _BV(SPIF)));
}

void DMdriver::init(DMLEDTABLE *table)
{
	pixel = new uint8_t[byteNum];  //init the table of actual pixels
	LATbit = digitalPinToBitMask(LATpin); // calculate LAT addresses
    LATport = digitalPinToPort(LATpin);
    LATreg = portModeRegister(LATport);
    LATout = portOutputRegister(LATport);
    DDRB |= _BV(PB3); // MOSI output
    DDRB |= _BV(PB5); // SCK output
    DDRB |= _BV(PB2); // SS output
    *LATreg |= LATbit; // LAT output

    PORTB &= ~_BV(PB5); // set SCK low

    SPSR = _BV(SPI2X); // double SPI speed
    SPCR = _BV(SPE) | _BV(MSTR);    // enable SPI, master mode, SCK low rising edge
	
	ledTable = table;
	
	clearAll();
	sendAll(); // found that this is useful to have a clear start
	
}

#ifdef DM256PWM
void DMdriver::setPoint(uint16_t pixNum, uint16_t pixVal)
{
	#ifdef DMCHAIN
	pixNum = getChainValue (pixNum);
	#endif
	
	if (ledTable) pixNum = ledTable[pixNum];

	pixel[pixNum] = pixVal;
}

uint16_t DMdriver::getPoint(uint16_t pixNum)
{
	#ifdef DMCHAIN
	pixNum = getChainValue (pixNum);
	#endif
	
	if (ledTable) pixNum = ledTable[pixNum];

	return pixel[pixNum];
}

void DMdriver::sendAll()
{
	LAT_low();
	//cli();
	uint16_t k = byteNum;
	if (DMtype==32) do
	{   k--;
		uint16_t pixVal = pixel[k] DM256PWM; // either square or <<8
		dm_shift(pixVal>>8);
		dm_shift(pixVal & 0xFF);
	} while (k);
	
	if (DMtype==24) do
	{   k--;
		uint16_t pixVal1 = (pixel[k] DM256PWM)>>4; 
		k--;
		uint16_t pixVal2 = (pixel[k] DM256PWM)>>4; 
		// shift MSB 8 bits, first value
		dm_shift(pixVal1>>4);
		// make second byte from LSB 4 bits of the first and MSB 4 bits of the second
		pixVal1 = (uint8_t)(pixVal1<<4) | (pixVal2 >> 8);
		dm_shift(pixVal1 & 0xFF);
		// shift LSB 8 bits of the second value
		dm_shift(pixVal2 & 0xFF);
	} while (k); 
	
	LAT_pulse();
}

#else  // end of DM256 stuff

void DMdriver::setPoint(uint16_t pixNum, uint16_t pixVal)
{
   #ifdef DMCHAIN
	pixNum = getChainValue (pixNum);
   #endif
   
   if (ledTable) pixNum = ledTable[pixNum];

  if (DMtype==24)  // 12-bit data shift
{
	
	uint16_t place = ((pixNum * 3) >> 1);
	if (pixNum & 1) 
	{ // starts clean
                      // 8 upper bits 
        pixel[place+1] = pixVal >> 4;
                               // 4 lower bits  | keep last 4 bits intact
        pixel[place] = ((uint8_t)(pixVal << 4)) | (pixel[place] & 0x0F);
   }
   else
   { // starts in the middle
                     // keep first 4 bits intact | 4 top bits 
        pixel[place+1] = (pixel[place+1] & 0xF0) | (pixVal >> 8);
		             // 8 lower bits of value
        pixel[place] = pixVal & 0xFF;
    } 
}
   else             // 16-bit data shift
{
	//uint16_t index = (pinNum-1) - pixNum;
	uint16_t place = (pixNum << 1);
	pixel[place+1] = pixVal >> 8;  // 8 top bits
	pixel[place] = pixVal & 0xFF; // 8 lower bits
}
}

uint16_t DMdriver::getPoint(uint16_t pixNum)
{
   #ifdef DMCHAIN
	pixNum = getChainValue (pixNum);
   #endif
   
   if (ledTable) pixNum = ledTable[pixNum];
	
  if (DMtype==24)  // 12-bit data 
{ 
	uint16_t place = ((pixNum * 3) >> 1);
	if (pixNum & 1) 
    { // starts clean
                 // 8 upper bits  | 4 lower bits
        return ((pixel[place+1]<<4) |  ((pixel[place] & 0xF0)>>4));
   
    }
    else
	{ // starts in the middle
                      // top 4 bits intact | lower 8 bits 
        return (((pixel[place+1] & 0xF)<<8)  | (pixel[place]));
        
    }
}
   else             // 16-bit data 
{
	uint16_t place = (pixNum << 1);
	return (pixel[place+1]<<8 | pixel[place]);
}
}

void DMdriver::sendAll()
{
	//cli();
	LAT_low();
	
	uint16_t k = byteNum;
	do
	{   k--;
	    dm_shift(pixel[k]);
	} while (k);
	
	LAT_pulse();
	//sei();
}
#endif

void DMdriver::setRGBpoint(uint16_t LED, uint16_t red, uint16_t green, uint16_t blue)
{
	LED *= 3;
		
	setPoint(LED, red);
	setPoint(LED+1, green);
	setPoint(LED+2, blue);
	
}

void DMdriver::setRGBmax(uint16_t LED, uint16_t red, uint16_t green, uint16_t blue, uint16_t max)
{
	if (max)
	{
		uint32_t valsum = (red + green + blue);
		if (valsum > max)
			{
				red = ((uint32_t)red * max) / valsum;
				green = ((uint32_t)green * max) / valsum;
				blue = ((uint32_t)blue * max) / valsum;
			}
	}
	
	LED *= 3;
	
	setPoint(LED, red);
	setPoint(LED+1, green);
	setPoint(LED+2, blue);
	
}

void DMdriver::setRGBled(uint16_t LED, uint16_t red, uint16_t green, uint16_t blue)
{
	setPoint(LED, red);
	setPoint(LED+1, green);
	setPoint(LED+2, blue);
}


void DMdriver::clearAll()
{
	for (uint16_t k=0; k<byteNum; k++)
	{pixel[k]=0;}
}

void DMdriver::setGlobalBrightness(uint8_t bri)
{

SPCR |= _BV(CPOL); //get and keep sck high

/*** 4 LAT pulses to turn on GBC input mode */
LAT_pulse(); LAT_pulse(); LAT_pulse(); LAT_pulse();
    
SPCR &= ~_BV(CPOL);    // sck low

/*** shift GBC data to the drivers
  * (each DM gets one byte; 7 upper bits control GB data, LSB should be 0 (o/s flag) 
  * BTW note that it IS possible to set different brightness for each DM chip
  */
for (uint8_t count=0; count<DMnum; count++)
{dm_shift(bri<<1);}

/*** pulse LAT to latch data */
LAT_pulse();

sendAll(); //needed because GBC somehow shifts in some PWM garbage too

}

void DMdriver::setGBCbyDriver(uint8_t *bri)
{

SPCR |= _BV(CPOL); //get and keep sck high

/*** 4 LAT pulses to turn on GBC input mode */
LAT_pulse(); LAT_pulse(); LAT_pulse(); LAT_pulse();
    
SPCR &= ~_BV(CPOL);    // sck low

for (uint8_t count=0; count<DMnum; count++)
{dm_shift((bri[DMnum-count-1]<<1));}

/*** pulse LAT to latch data */
LAT_pulse();

sendAll(); //needed because GBC somehow shifts in some PWM garbage too

}

uint16_t DMdriver::getChainValue (uint16_t pixNum)
{
	uint8_t numSeg = DMnum / 3;
	uint8_t allStops = numSeg*2 - 1;
	uint16_t DMstep = 8 * 3;
	uint16_t curSeg = pixNum / DMstep;
	
	if (pixNum < (pinNum>>1)) pixNum = pixNum + DMstep * curSeg;
	else pixNum = (allStops - curSeg) * (DMstep<<1) + DMstep + pixNum%DMstep;
	return pixNum;
}

void DMdriver::turnOff()
{
	delete [] pixel; // free the allocated array memory
}

void DMdriver::deallocLedTable()
{
	delete [] ledTable; // free the ledtable memory
}

