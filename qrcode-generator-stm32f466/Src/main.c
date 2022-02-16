/*
 * main.c
 *
 *  Created on: Oct 16, 2020
 *      Author: am
 */

#include "stm32f446xx.h"
#include "stdlib.h"
#include "string.h"
#include "qrcode.h"

typedef struct {
	uint8_t *pui8Buffer;
	uint16_t ui16Capacity;
	uint16_t ui16Size;
	uint16_t ui16Tail;
	uint16_t ui16Head;
} Queue_t;

// Code will stop if the condition is not met, and disable all interrupts
#define dAssert(condition) if(!(condition)){__disable_irq(); while(1){}};

#define FALSE 0
#define TRUE  1

#define QRCODE_VERSION 3

Queue_t stcQueueRx;
Queue_t stcQueueTx;

// Should be 2^x
#define RX_QUEUE_LENGTH 16
#define TX_QUEUE_LENGTH 1024
uint8_t arrui8QueueBufferRx[RX_QUEUE_LENGTH];
uint8_t arrui8QueueBufferTx[TX_QUEUE_LENGTH];

uint8_t arrui8LocalReceiveBuffer[1024];

#define dReceivedCommand(bufferptr)  *((uint16_t*) (bufferptr+1))
#define dReceivedLength(bufferptr)  *bufferptr

// Please add to the list according to the parsers switchcase
enum COMMANDS {
	INIT = 0, RECEIVE = 1, VALIDATE = 2, CLEANUP = 3, CMD_GENERATE_QR = 0x000A
};

// "static initialization"
//#define dInitStaticQueue(PQUEUE,CAPACITY) Queue_t PQUEUE = {.pui8Buffer= (uint8_t[]){[0 ... CAPACITY-1]=0x00}, .ui16Capacity=CAPACITY, .ui16Tail=0, .ui16Head=0};
//dInitStaticQueue(queuetx,TX_QUEUE_LENGTH);
//dInitStaticQueue(queuerx,TX_QUEUE_LENGTH);

// Code based init
#define dInitQueue(queue, buffer, capacity) {queue.pui8Buffer=buffer; queue.ui16Capacity = capacity; queue.ui16Tail = 0; queue.ui16Head = 0; queue.ui16Size=0;}

// Queue Operations
#define dPutByteIntoQueue(queue, byte){  dAssert(queue.ui16Size<queue.ui16Capacity); queue.pui8Buffer[queue.ui16Head] = byte; queue.ui16Head = (queue.ui16Head +1)&(queue.ui16Capacity-1); NVIC_DisableIRQ(TIM2_IRQn); ++queue.ui16Size;NVIC_EnableIRQ(TIM2_IRQn);}
#define dPutBufferIntoQueue(queue, buffer,bytecount){ dAssert(queue.ui16Size+bytecount<=queue.ui16Capacity);\
													  for(uint16_t j=queue.ui16Head,i = 0;i<bytecount;i++,j=(j+1)&(queue.ui16Capacity-1) ){\
														  *(queue.pui8Buffer+j) = buffer[i];   }\
													  queue.ui16Head = (queue.ui16Head + bytecount)&(queue.ui16Capacity-1); NVIC_DisableIRQ(TIM2_IRQn);queue.ui16Size +=bytecount;NVIC_EnableIRQ(TIM2_IRQn); }

#define dPeekFirstByte(queue, byte){ byte = *(queue.pui8Buffer+queue.ui16Tail);}
#define dRemoveByteFromQueue(queue,byte)	{dAssert(queue.ui16Size>0); byte = *(queue.pui8Buffer+queue.ui16Tail); queue.ui16Size -=1; queue.ui16Tail = (queue.ui16Tail+1)&(queue.ui16Capacity-1);}

// Queue fill
#define dGetQueueSize(queue) (queue.ui16Size)

// Remove from queue
#define dRemoveFromQueue(queue, buffer, bufferoffset, bytecount) {      dAssert(queue.ui16Size>=bytecount); \
																		for(uint16_t i=0,j=queue.ui16Tail;i<bytecount;i++,j=(j+1)&(queue.ui16Capacity-1)){\
																		buffer[i+bufferoffset] = *(queue.pui8Buffer+j);}\
																		queue.ui16Tail =(queue.ui16Tail + bytecount)&(queue.ui16Capacity-1); \
																		NVIC_DisableIRQ(USART2_IRQn); 	queue.ui16Size-=bytecount; NVIC_EnableIRQ(USART2_IRQn); }

#define dClearQueue(queue) { NVIC_DisableIRQ(USART2_IRQn); queue.ui16Tail = (queue.ui16Tail+queue.ui16Size)&(queue.ui16Capacity-1); queue.ui16Size = 0; NVIC_EnableIRQ(USART2_IRQn);}

#define dWrite(x) if(!(USART2->SR  &USART_SR_TXE)){}USART2->DR = x;

/**/

uint16_t ui16gUartTimeout = 0;

/* Not Used*/
void TIM2_IRQHandler() {
	uint16_t ui16Temp;
	uint8_t ui8SingleData;

	if (TIM2->SR & TIM_SR_UIF) {
		ui16Temp = dGetQueueSize(stcQueueTx);
		if (ui16Temp > 0) {
			if (USART2->SR & USART_SR_TXE) {
				dRemoveByteFromQueue(stcQueueTx, ui8SingleData);
				USART2->DR = ui8SingleData;
			}

		}

		TIM2->SR &= ~(TIM_SR_UIF);
	}

	if(ui16gUartTimeout>0){
		--ui16gUartTimeout;
	}
}
;

/* Receive Rx Interrupts*/
void USART2_IRQHandler() {

	// Is there is new data, put it into queue
	// Bit is cleared by reading DR
	if (USART2->SR & USART_SR_RXNE) {
		dPutByteIntoQueue(stcQueueRx, USART2->DR);
	}
}

/* Initialize Clocks, Pins and Func. Units*/
void vInitializePeripherals() {
	// GPIO Clocks
	//----------------------------------
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

	// Debug Pin
	//---------------------------------
	GPIOC->MODER |= GPIO_MODER_MODE10_0;	// Out
	GPIOC->BSRR |= GPIO_BSRR_BS10;

	// Uart
	//-----------------------------
	RCC->APB1ENR |= RCC_APB1ENR_USART2EN; 	// 16 Mhz not divided for PCLK1 and PCLK2
	GPIOA->MODER &= ~(GPIO_MODER_MODE3 | GPIO_MODER_MODE2);
	GPIOA->MODER |= GPIO_MODER_MODE3_1 | GPIO_MODER_MODE2_1;					// Alternative function for TX and RX
	GPIOA->OSPEEDR |= (GPIO_OSPEEDER_OSPEEDR2 | GPIO_OSPEEDER_OSPEEDR3);			// Max speed
	GPIOA->AFR[0] = GPIO_AFRL_AFSEL3_0 | GPIO_AFRL_AFSEL3_1 | GPIO_AFRL_AFSEL3_2;
	GPIOA->AFR[0] |= GPIO_AFRL_AFSEL2_0 | GPIO_AFRL_AFSEL2_1 | GPIO_AFRL_AFSEL2_2;

	// Baud Calculation
	// Baud = Clock/(8 x (2-OVER8) x USARTDIV )  ->
	USART2->BRR = (((uint32_t) 16000000) / (115200));
	USART2->CR1 = 0 | USART_CR1_TE | USART_CR1_RE;	// Enable Communication
	USART2->CR1 |= USART_CR1_RXNEIE;				// Enable RX Interrupt

	NVIC_ClearPendingIRQ(USART2_IRQn);
	NVIC_EnableIRQ(USART2_IRQn);

	USART2->CR1 |= USART_CR1_UE;					// Enable Uart

	// TIM2
	//--------------------------------
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

	TIM2->CR1 = 0 | TIM_CR1_DIR;	// Count down
	TIM2->CNT = 0;

	TIM2->PSC = 16;
	TIM2->ARR = 1000;
	TIM2->CR1 |= TIM_CR1_CEN;		// 1 ms Timer

	NVIC_ClearPendingIRQ(TIM2_IRQn);
	NVIC_EnableIRQ(TIM2_IRQn);

	TIM2->DIER = 0 | TIM_DIER_UIE;

}

int main() {
	uint16_t ui16RxBufferOffset = 0;
	uint8_t ui8TempRxByteCount;
	uint8_t arrui8QrcodeData[qrcode_getBufferSize(QRCODE_VERSION)];
	QRCode stcQrcode;

	memset(arrui8QrcodeData, 0, sizeof(arrui8QrcodeData));
	dInitQueue(stcQueueRx, arrui8QueueBufferRx, sizeof(arrui8QueueBufferRx));	// Queues
	dInitQueue(stcQueueTx, arrui8QueueBufferTx, sizeof(arrui8QueueBufferTx));
	vInitializePeripherals();			// Periphs

	// Generate QR code from text
	// @Debug print
	/*
	 qrcode_initText(&stcQrcode, arrui8QrcodeData, 3, 0, "HELLO WORLD");
	 for (uint8_t y = 0; y < stcQrcode.size; y++) {
		 dWrite(' ');
		 dWrite(' ');
		 dWrite(' ');
		 dWrite(' ');
		 // Each horizontal module
		 for (uint8_t x = 0; x < stcQrcode.size; x++) {

			 // Print each module (UTF-8 \u2588 is a solid block)
			 if (qrcode_getModule(&stcQrcode, x, y)) {
				 dWrite('x');
			 } else {
				 dWrite(' ');
			 }
		 }
		 dWrite('\n');
	 }
	}*/

	uint16_t ui16ParserState = 0;
	uint8_t * pui8CurrentData = NULL;
	uint8_t ui8Length = 0;
	while (1) {

		ui8TempRxByteCount = dGetQueueSize(stcQueueRx);

		// Parser State Machine
		switch (ui16ParserState) {
		case INIT:
			if (ui8TempRxByteCount > 0) {

				ui16gUartTimeout = 2000;	// Timeout is 2 seconds
				dPeekFirstByte(stcQueueRx, ui8Length);

				// if Valid Length: there has ben at least one byte payload
				if (ui8Length > 4) {
					pui8CurrentData = malloc(ui8Length);		// create a bit of space for length and cmd

					dAssert(pui8CurrentData != NULL);
					++ui16ParserState;

				} else {
					dClearQueue(stcQueueRx);
				}
			}
			break;

			// Reception
		case RECEIVE:
			if (ui8TempRxByteCount > 0) {

				// Check how many bytes are left
				// if There are more than we need then we receive only as much as we need
				if (ui8TempRxByteCount >= ui8Length) {
					dRemoveFromQueue(stcQueueRx, pui8CurrentData, ui16RxBufferOffset, ui8Length);
					++ui16ParserState;
					// less bytes than what we need
				} else {
					ui8Length -= ui8TempRxByteCount;
					dRemoveFromQueue(stcQueueRx, pui8CurrentData, ui16RxBufferOffset, ui8TempRxByteCount);
					ui16RxBufferOffset += ui8TempRxByteCount;
				}
			}
			break;

			// Jump to command
		case VALIDATE:
			ui16ParserState = dReceivedCommand(pui8CurrentData) >= CMD_GENERATE_QR ? dReceivedCommand(pui8CurrentData) : CLEANUP;	// Validate command
			break;

			// done
		case CLEANUP:
		default:
			free(pui8CurrentData);
			ui16RxBufferOffset = 0;
			ui16ParserState = INIT;
			pui8CurrentData = NULL;
			break;

			// Commands
		case CMD_GENERATE_QR:
			qrcode_initBytes(&stcQrcode, arrui8QrcodeData, QRCODE_VERSION, 0, &pui8CurrentData[3],
			dReceivedLength(pui8CurrentData) - 4);		// Generate qrcode for payload minus checksum

			ui8Length = sizeof(arrui8QrcodeData) + 1 + 4;		// Calculate size of the frame length + command + qrsize + qrarray

			dPutByteIntoQueue(stcQueueTx, ui8Length)
			;
			dPutByteIntoQueue(stcQueueTx, 0)
			;
			dPutByteIntoQueue(stcQueueTx, (uint8_t )CMD_GENERATE_QR)
			;

			dPutByteIntoQueue(stcQueueTx, stcQrcode.size)
			;
			dPutBufferIntoQueue(stcQueueTx, arrui8QrcodeData, sizeof(arrui8QrcodeData))
			;

			dPutByteIntoQueue(stcQueueTx, 0x00)
			;		// TODO Checksum of Payload

			ui16ParserState = CLEANUP;
			break;
		}

		// Timeout here
		// taking too long, we need to reset the interface
		if(!ui16gUartTimeout && ui16ParserState != INIT){
			ui16ParserState = CLEANUP;
		}
	}

// Idea
// Create limited Heap
// Create Queue for data
// Put add from Queue into heap
// Calculate crc16 and send it back

	return 1;
}
