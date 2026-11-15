
#include "circbuffer.h"

void circbuf_setup(Circular_Buffer *entry, uint8_t *buf_array, int buf_size)
{
   entry->buffer = buf_array;
   entry->read_index = 0;
   entry->write_index = 0;
   entry->buffer_used = 0;
   entry->buffer_size = buf_size;
}

void circbuf_write(Circular_Buffer *entry, uint8_t datum)
{
   if(entry->buffer_used >= entry->buffer_size)
      return;
   entry->buffer[entry->write_index] = datum;
   entry->buffer_used++;
   entry->write_index++;
   if(entry->write_index >= entry->buffer_size)
      entry->write_index = 0;
   return;
}

uint8_t circbuf_read(Circular_Buffer *entry)
{
   uint8_t datum;
   if(entry->buffer_used <= 0)
      return 0xFF;
   datum = entry->buffer[entry->read_index];
   entry->buffer_used--;
   entry->read_index++;
   if(entry->read_index >= entry->buffer_size)
      entry->read_index = 0;
   return datum;
}


//circbuf_setup(&UART_Receive,UART_Receive_Buffer,UART_BUFFER_SIZE);
//circbuf_setup(&UART_Transmit,UART_Transmit_Buffer,UART_BUFFER_SIZE);


