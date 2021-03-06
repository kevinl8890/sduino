/*
  HardwareSerial.c - Hardware serial library for plainduino
  Copyright (c) 2016 Michael Mayer

  Plain C version of HardwareSerial.cpp of the Arduino project.
  Copyright (c) 2006 Nicholas Zambetti.  All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
  
  Modified 23 November 2006 by David A. Mellis
  Modified 28 September 2010 by Mark Sproul
  Modified 14 August 2012 by Alarus
*/


#include"stm8s.h"

//#include "c-HardwareSerial.h"


#define SERIAL_BUFFER_SIZE 16
typedef struct ring_buffer
{
  unsigned char buffer[SERIAL_BUFFER_SIZE];
  volatile unsigned int head;
  volatile unsigned int tail;
} ring_buffer;

ring_buffer rx_buffer = { { 0 }, 0, 0};
ring_buffer tx_buffer = { { 0 }, 0, 0};

void store_char(unsigned char c, ring_buffer *buffer)
//inline void store_char(unsigned char c, ring_buffer *buffer)
{
  int i = (unsigned int)(buffer->head + 1) % SERIAL_BUFFER_SIZE;

  // if we should be storing the received character into the location
  // just before the tail (meaning that the head would advance to the
  // current location of the tail), we're about to overflow the buffer
  // and so we don't write the character or advance the head.
  if (i != buffer->tail) {
    buffer->buffer[buffer->head] = c;
    buffer->head = i;
  }
}

volatile char	transmitting=0;

// Interrupt handler ///////////////////////////////////////////////////////////

void UART1_RX_IRQHandler(void) __interrupt(ITC_IRQ_UART1_RX) /* UART1 RX */
{
    unsigned char c;

    c = UART1_ReceiveData8();
    // check for parity error
    if (!UART1_GetFlagStatus(UART1_FLAG_PE)) {
        // no parity error, so store the data
        store_char(c, &rx_buffer);
    };
}


void UART1_TX_IRQHandler(void) __interrupt(ITC_IRQ_UART1_TX) /* UART1 TX */
{
  if (tx_buffer.head == tx_buffer.tail) {
	// Buffer empty, so disable interrupts
        transmitting = 0;
        UART1_ITConfig(UART1_IT_TXE, DISABLE);
  }
  else {
    // There is more data in the output buffer. Send the next byte
    unsigned char c = tx_buffer.buffer[tx_buffer.tail];
    tx_buffer.tail = (tx_buffer.tail + 1) % SERIAL_BUFFER_SIZE;
	
    UART1_SendData8(c);
  }
}

// Public Methods //////////////////////////////////////////////////////////////

void HardwareSerial_begin(unsigned long baud)
{
  //set the data bits, parity, and stop bits
  UART1_Init(baud,
      UART1_WORDLENGTH_8D, UART1_STOPBITS_1, UART1_PARITY_NO,
      UART1_SYNCMODE_CLOCK_DISABLE, UART1_MODE_TXRX_ENABLE);

  UART1_ITConfig(UART1_IT_RXNE, ENABLE);	// enable RXNE interrupt
}


void HardwareSerial_begin_config(unsigned long baud, uint8_t config)
{
  UART1_StopBits_TypeDef	stopbits;
  UART1_Parity_TypeDef		parity;
  UART1_WordLength_TypeDef	wordlength;

  wordlength = (config & 0x10) ? UART1_WORDLENGTH_9D : UART1_WORDLENGTH_8D;
  stopbits   = (config & 0x03) ? UART1_STOPBITS_2   : UART1_STOPBITS_1;
  parity=UART1_PARITY_NO;	// default
  config &= 0xc;
  if      (config == 0x8) parity=UART1_PARITY_EVEN;
  else if (config == 0xc) parity=UART1_PARITY_ODD;
  
  //set the data bits, parity, and stop bits
  UART1_Init(baud, wordlength, stopbits, parity, 
      UART1_SYNCMODE_CLOCK_DISABLE, UART1_MODE_TXRX_ENABLE);

  UART1_ITConfig(UART1_IT_RXNE, ENABLE);	// enable RXNE interrupt
}


void HardwareSerial_end(void)
{
  // wait for transmission of outgoing data
  while (tx_buffer.head != tx_buffer.tail)
    ;

  UART1_DeInit();
  
  // clear any received data
  rx_buffer.head = rx_buffer.tail;
}

int HardwareSerial_available(void)
{
  return (unsigned int)(SERIAL_BUFFER_SIZE + rx_buffer.head - rx_buffer.tail) % SERIAL_BUFFER_SIZE;
}

int HardwareSerial_peek(void)
{
  if (rx_buffer.head == rx_buffer.tail) {
    return -1;
  } else {
    return rx_buffer.buffer[rx_buffer.tail];
  }
}

int HardwareSerial_read(void)
{
  // if the head isn't ahead of the tail, we don't have any characters
  if (rx_buffer.head == rx_buffer.tail) {
    return -1;
  } else {
    unsigned char c = rx_buffer.buffer[rx_buffer.tail];
    rx_buffer.tail = (unsigned int)(rx_buffer.tail + 1) % SERIAL_BUFFER_SIZE;
    return c;
  }
}

void HardwareSerial_flush(void)
{
  // UDR is kept full while the buffer is not empty, so TXC triggers when
  // EMPTY && SENT

//  while (transmitting && ! (*_ucsra & _BV(TXC0)));
//  while (transmitting && ! (UART1_SR & UART1_SR_TC));
  while (transmitting && ! UART1_GetFlagStatus(UART1_FLAG_TC));
  transmitting = 0;
}

//size_t 
int HardwareSerial_write(uint8_t c)
{
  int i = (tx_buffer.head + 1) % SERIAL_BUFFER_SIZE;
	
  // If the output buffer is full, there's nothing for it other than to 
  // wait for the interrupt handler to empty it a bit
  // ???: return 0 here instead?
  while (i == tx_buffer.tail)
    ;
	
  tx_buffer.buffer[tx_buffer.head] = c;
  tx_buffer.head = i;

  UART1_ITConfig(UART1_IT_TXE, ENABLE);		// enable TXE interrupt
  transmitting = 1;
  //FIXME: unklar, ob das auf dem STM8 wirklich nötig ist.
  // Das TXE-Bit in UART_SR ist jedenfalls nur lesbar.
  // clear the TXC bit -- "can be cleared by writing a one to its bit location"
//  sbi(*_ucsra, TXC0);
  
  return 1;
}


void send_string(char *str)
{
    char c;

    if (!str) return;

    while ( c=*str++ ) HardwareSerial_write(c);	// assignment intented
}


void main (void)
{
    uint32_t i;

    UART1_DeInit();
    enableInterrupts();

    HardwareSerial_begin(9600);

    while (1) {
        send_string("Hello World!\r\n");
        for (i=15000; i; i--);
        while(HardwareSerial_available()) {
            HardwareSerial_write('.');
            HardwareSerial_write(HardwareSerial_read());
        };
        for (i=15000; i; i--);
    }
}
