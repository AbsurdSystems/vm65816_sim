
#include<stdint.h>

typedef struct Circular_Buffer
{
   uint8_t *buffer;
   int read_index;
   int write_index;
   int buffer_used; 
   int buffer_size;
} Circular_Buffer;


void circbuf_setup(Circular_Buffer *entry, uint8_t *buf_array, int buf_size);
void circbuf_write(Circular_Buffer *entry, uint8_t datum);
uint8_t circbuf_read(Circular_Buffer *entry);

//circbuf_setup(&UART_Receive,UART_Receive_Buffer,UART_BUFFER_SIZE);
//circbuf_setup(&UART_Transmit,UART_Transmit_Buffer,UART_BUFFER_SIZE);


