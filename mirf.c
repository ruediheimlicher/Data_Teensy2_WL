/*
	Copyright (c) 2007 Stefan Engelke <mbox@stefanengelke.de>
	
	Permission is hereby granted, free of charge, to any person 
	obtaining a copy of this software and associated documentation 
	files (the "Software"), to deal in the Software without 
	restriction, including without limitation the rights to use, copy, 
	modify, merge, publish, distribute, sublicense, and/or sell copies 
	of the Software, and to permit persons to whom the Software is 
	furnished to do so, subject to the following conditions:
	
	The above copyright notice and this permission notice shall be 
	included in all copies or substantial portions of the Software.
	
	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
	NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT 
	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, 
	WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER 
	DEALINGS IN THE SOFTWARE.
	
	$Id$
	
	-----
	
	Modified by Alex from insideGadgets (http://www.insidegadgets.com)
	Last Modified: 30/12/2012
*/

#include "mirf.h"
#include "nRF24L01.h"
#include <avr/io.h>
#include <avr/interrupt.h>

// Defines for setting the MiRF registers for transmitting or receiving mode
#define TX_POWERUP mirf_config_register(CONFIG, mirf_CONFIG | ( (1<<PWR_UP) | (0<<PRIM_RX) ) )
#define RX_POWERUP mirf_config_register(CONFIG, mirf_CONFIG | ( (1<<PWR_UP) | (1<<PRIM_RX) ) )
#define POWERDOWN mirf_config_register(CONFIG, mirf_CONFIG | ( (0<<PWR_UP) ) )

extern uint8_t data_out[];

// Initializes pins ans interrupt to communicate with the MiRF module
// Should be called in the early initializing phase at startup.
void mirf_init(void) {
	// Define CSN and CE as Output and set them to default
	DDRB |= (1<<CE);
	DDRB |= (1<<CSN);
	mirf_CE_lo;
	mirf_CSN_hi;
	
	spi_init(); // Initialise SPI
}

// Sets the important registers in the MiRF module and powers the module
// in receiving mode
void mirf_config(void) {
	// Set RF channel
	mirf_config_register(RF_CH, mirf_CH);
	
	// Set length of incoming payload 
	mirf_config_register(RX_PW_P0, mirf_PAYLOAD);
	mirf_config_register(RX_PW_P1, mirf_PAYLOAD);
	
	// Set auto retransmit delay to 500uS and leave auto retransmit count to 3
	mirf_config_register(SETUP_RETR, (1<<4) | 3);
	
	// Set RADDR and TADDR
	mirf_write_register(RX_ADDR_P0, TADDR, 5);
	mirf_write_register(RX_ADDR_P1, RADDR, 5);
	mirf_write_register(TX_ADDR, TADDR, 5);
	
	// Enable RX_ADDR_P0 and RX_ADDR_P1 address matching since we also enable auto acknowledgement
	mirf_config_register(EN_RXADDR, (1<<ERX_P0 | 1<<ERX_P1));
	
	// Power up in transmitter mode
	TX_POWERUP;
}

// Read the status register
uint8_t mirf_status(void) {
	mirf_CSN_lo; // Pull down chip select
	spi_transfer(R_REGISTER | (REGISTER_MASK & STATUS));
	uint8_t status = spi_transfer(NOP); // Read status register
	mirf_CSN_hi; // Pull up chip select
	return status;
}

// Checks if data is available for reading
uint8_t mirf_data_ready(void) {
	mirf_CSN_lo; // Pull down chip select
	spi_transfer(R_REGISTER | (REGISTER_MASK & STATUS));
	uint8_t status = spi_transfer(NOP); // Read status register
	mirf_CSN_hi; // Pull up chip select
	return status & (1<<RX_DR);
}

// Checks if MAX_RT has been reached
uint8_t mirf_max_rt_reached(void) {
	mirf_CSN_lo; // Pull down chip select
	spi_transfer(R_REGISTER | (REGISTER_MASK & STATUS));
	uint8_t status = spi_transfer(NOP); // Read status register
	mirf_CSN_hi; // Pull up chip select
	return status & (1<<MAX_RT);
}

// Reads mirf_PAYLOAD bytes into data array
void mirf_get_data(uint8_t *data) {
	mirf_CSN_lo; // Pull down chip select
	spi_transfer(R_RX_PAYLOAD); // Send cmd to read rx payload
	spi_read_data(data, mirf_PAYLOAD); // Read payload
	mirf_CSN_hi; // Pull up chip select
	mirf_config_register(STATUS,(1<<RX_DR)); // Reset status register
}

// Write one byte into the MiRF register
void mirf_config_register(uint8_t reg, uint8_t value) {
	mirf_CSN_lo;
	spi_transfer(W_REGISTER | (REGISTER_MASK & reg));
	spi_transfer(value);
	mirf_CSN_hi;
}

// Reads an array of bytes from the MiRF registers.
void mirf_read_register(uint8_t reg, uint8_t *value, uint8_t len) {
	mirf_CSN_lo;
	spi_transfer(R_REGISTER | (REGISTER_MASK & reg));
	spi_read_data(value, len);
	mirf_CSN_hi;
}

// Writes an array of bytes into the MiRF register
void mirf_write_register(uint8_t reg, uint8_t *value, uint8_t len) {
	mirf_CSN_lo;
	spi_transfer(W_REGISTER | (REGISTER_MASK & reg));
	spi_write_data(value, len);
	mirf_CSN_hi;
}

// Send data and wait for an ACK
uint8_t mirf_transmit_data(void) {
	TX_POWERUP; // Power up to transmitter mode
	
	mirf_CSN_lo; // Pull down chip select
	spi_transfer(FLUSH_TX); // Write cmd to flush tx fifo
	mirf_CSN_hi; // Pull up chip select
	
	mirf_CSN_lo;  // Pull down chip select
	spi_transfer(W_TX_PAYLOAD); // Write cmd to write payload
	spi_write_data(data_out, mirf_PAYLOAD); // Write payload
	mirf_CSN_hi; // Pull up chip select
	
	mirf_CE_hi; // Start transmission
	_delay_us(15);
	mirf_CE_lo;
	
	int waitcount = 0;
	while (!(mirf_status() & (1<<TX_DS))) { // Wait until we receive an ACK back
		_delay_us(500); // Wait the auto retransmit time
		if (mirf_status() & (1<<MAX_RT)) { // If maximum retries reached
			mirf_config_register(STATUS, 1<<MAX_RT); // Reset maximum retries to be able to transmit again next time
			mirf_CE_hi;
			_delay_us(15);
			mirf_CE_lo;
		}
		waitcount++;
		if (waitcount >= 10) { // If we haven't heard back in 5ms, exit
			mirf_config_register(STATUS, 1<<MAX_RT); // Reset maximum retries to be able to transmit again next time
			return 0;
		}
	}
	
	mirf_config_register(STATUS,(1<<TX_DS)); // Reset status register
	
	return 1;
}

// SPI transfer 1 byte and return the result
uint8_t spi_transfer(uint8_t data)
{
   SPDR = data;
   
   while(!(SPSR & (1<<SPIF)));
   
   return SPDR;
}

// Write data using SPI
void spi_write_data(uint8_t * dataout, uint8_t len) {
   uint8_t i;
   for (i = 0; i < len; i++) {
      spi_transfer(dataout[i]);
   }
}

// Read data using SPI
void spi_read_data(uint8_t * datain, uint8_t len) {
   uint8_t i;
   for (i = 0; i < len; i++) {
      datain[i] = spi_transfer(0x00);
   }
}
