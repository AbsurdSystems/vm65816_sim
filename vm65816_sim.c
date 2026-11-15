// Build this application by using the make command
// > make
// sudo apt-get install libncurses-dev


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <curses.h>
#include "io_peripherals.h"
#include "circbuffer.h"
#include "shared.h"
#include "wdc65c816.h"

#define MEMORY_SIZE   0x100000

#define DUMP_BYTES_IN_ROW 16
#define IN_BUF_SIZE 100
#define OUT_TOK_SIZE 25
#define SERIALIO_CHECK_FREQ 15
#define SIDEWIN_WIDTH 77
#define SIDEWIN_MIDDLE_LOC (LINES/2)
#define UART_BUFFER_SIZE  128

#define COP_BUF_SIZE  32
typedef union COP_Parameter_Array
{
   uint32_t dword[COP_BUF_SIZE/4];
   uint16_t word[COP_BUF_SIZE/2];
   uint8_t byte[COP_BUF_SIZE];
} cop_param_array;

cop_param_array cop_regs;

Circular_Buffer UART_Receive;

uint8_t UART_Receive_Buffer[UART_BUFFER_SIZE];

//typedef unsigned int uint32_t;
//typedef unsigned int uint16_t;
//typedef unsigned char uint8_t;

uint8_t run_flag_65816;


WDC65C816_Regs vm_regs;


//SRAM Memory array
uint8_t memory_array[MEMORY_SIZE];

shuint32 breakpoint_addr;
//uint16_t breakpoint_addr = 0;

//Ncursrs main window
WINDOW * mainwin;
WINDOW * sidewin;


void print_serialio_chars(uint8_t charvalue);


uint8_t readio_byte_65816(uint8_t io_addr)
{
    switch(io_addr)
    {
       case 0x00:  
         return(circbuf_read(&UART_Receive));
       case 0x01:  
         return(UART_Receive.buffer_used);
       default:
         return(0);
    }
}


void writeio_byte_65816(uint8_t io_addr, uint8_t value)
{
    switch(io_addr)
    {
       case 0x00:  
         print_serialio_chars(value);
         return;
       default:
         return;
    }
}


uint8_t read_byte_65816(uint8_t bank, uint16_t address)
{
   shuint32 temp_address;
   temp_address.dword = (uint32_t) address;
   temp_address.byte[NATIVE_BYTE_UPPER] = bank;
   //address_bus = address;
   if((temp_address.dword & 0xFFFF00) == 0x8000)
   {
      return readio_byte_65816(address & 0xFF);
   }

   return memory_array[(temp_address.dword & (MEMORY_SIZE-1))];
}

void write_byte_65816(uint8_t bank, uint16_t address, uint8_t value)
{
   shuint32 temp_address;
   temp_address.dword = (uint32_t) address;
   temp_address.byte[NATIVE_BYTE_UPPER] = bank;
   //address_bus = address;
   if((temp_address.dword & 0xFFFF00) == 0x8000)
   {
      writeio_byte_65816(address & 0xFF, value);
      return;
   }
   memory_array[(temp_address.dword & (MEMORY_SIZE-1))] = value;
   return;
}


void instr_wdm_65816(WDC65C816_Regs *regs, uint8_t opcode)
{
   shuint16 tempval;

   switch(opcode)
   {
     case IOFUNC_WRITEBYTE_COPREG: 
        cop_regs.byte[reg_X] = reg_A;
        return;
     case IOFUNC_READBYTE_COPREG: 
        reg_A = cop_regs.byte[reg_X]; 
        update_flag((reg_A == 0),FLAG_ZERO);
        update_flag(((reg_A & 0x80) == 0x80),FLAG_SIGN);
        return;
     case IOFUNC_WRITEWORD_COPREG: 
        cop_regs.word[reg_X] = reg_C;
        return;
     case IOFUNC_READWORD_COPREG: 
        reg_C = cop_regs.word[reg_X]; 
        update_flag((reg_C == 0),FLAG_ZERO);
        update_flag(((reg_B & 0x80) == 0x80),FLAG_SIGN);
        return;
     case IOFUNC_UART_SERIN:   //EhBasic simulated ASIC input
        reg_A = circbuf_read(&UART_Receive);
        update_flag((reg_A == 0),FLAG_ZERO);
        update_flag(((reg_A & 0x80) == 0x80),FLAG_SIGN);
        return;
     case IOFUNC_UART_SEROUT:   //EhBASIC simulated ASIC output
        print_serialio_chars(reg_A);
        return;
     case IOFUNC_UART_AVAIL:
        reg_A = UART_Receive.buffer_used;
        update_flag((reg_A == 0),FLAG_ZERO);
        update_flag(((reg_A & 0x80) == 0x80),FLAG_SIGN);
        return;
     default:
        return;
   }
}

void instr_stp_65816(WDC65C816_Regs *regs)
{
   run_flag_65816 = 0;
}

void instr_wai_65816(WDC65C816_Regs *regs)
{
   run_flag_65816 = 0;
}




////////////////////////////////////////////////////////////////////////
//  Command interface
////////////////////////////////////////////////////////////////////////


char in_buffer[IN_BUF_SIZE];


void get_command_str(WINDOW *w, int y, int x, const char *prompt, char *string_buf, int buf_size)
{
   echo();
   //nodelay(w, FALSE);
   mvwprintw(w, y, x, " ");
   for(int i = 0; i < (SIDEWIN_WIDTH-2); i++)
   {
      wprintw(w, " ");
   } 
   mvwprintw(w, y, x, prompt);
   wgetnstr(w, string_buf, buf_size);
   noecho();
   //nodelay(w, TRUE);
}


void clear_message_win()
{
   for(int i = SIDEWIN_MIDDLE_LOC+2; i < LINES; i++)
   {
      for(int j = 1; j < SIDEWIN_WIDTH; j++)
      {
         mvwaddch(sidewin,i,j,' ');
      }
   }
   wrefresh(sidewin);
}


void clear_main_win()
{
   for(int i = 0; i < LINES; i++)
   {
      for(int j = 0; j < (COLS-SIDEWIN_WIDTH); j++)
      {
         mvwaddch(mainwin,i,j,' ');
      }
   }
   wmove(mainwin,0,0);
   wrefresh(mainwin);
}


void dumpmem(WDC65C816_Regs *regs, uint32_t startaddress, uint32_t endaddress)
{
   uint32_t i, j;
   uint8_t value;
   shuint32 temp_address;
   int line_num = SIDEWIN_MIDDLE_LOC+2;
   mvwprintw(sidewin,line_num,1,"          ");
   for(j=0; j<DUMP_BYTES_IN_ROW; j++)
   {
      wprintw(sidewin,"-%02X",j);
   }
   line_num++;
   for(i=startaddress; i<=endaddress; i+=DUMP_BYTES_IN_ROW)
   {
      mvwprintw(sidewin,line_num,1,"[%06X]: ",i);
      for(j=0; j<DUMP_BYTES_IN_ROW; j++)
      {
         temp_address.dword = i+j;
	     if((reg_PC == temp_address.word[NATIVE_WORD_LOW]) && (reg_PB == temp_address.byte[NATIVE_BYTE_UPPER]))
            wprintw(sidewin,"*%02X",read_byte_65816(temp_address.byte[NATIVE_BYTE_UPPER], temp_address.word[NATIVE_WORD_LOW] ));
    	 else
            wprintw(sidewin," %02X",read_byte_65816(temp_address.byte[NATIVE_BYTE_UPPER], temp_address.word[NATIVE_WORD_LOW]));
      }
      wprintw(sidewin,"  ");
      for(j=0; j<DUMP_BYTES_IN_ROW; j++)
      {
         temp_address.dword = i+j;
         value = read_byte_65816(temp_address.byte[NATIVE_BYTE_UPPER], temp_address.word[NATIVE_WORD_LOW]);
	 if(value >= 32 && value <= 126)
            wprintw(sidewin,"%c",value);
	 else
            wprintw(sidewin,".");
      }
      line_num++;
   }
   wrefresh(sidewin);
}




uint8_t fetch_hex_number(FILE *fp)
{
   uint8_t tempval = 0;
   uint8_t in_char = 0;
   in_char = toupper(fgetc(fp) & 0x7F);
   if(in_char >= '0' && in_char <= '9')
   {
      tempval = (in_char - '0') << 4;
   }
   if(in_char >= 'A' && in_char <= 'F')
   {
      tempval = ((in_char - 'A')+10) << 4;
   }
   in_char = toupper(fgetc(fp) & 0x7F);
   if(in_char >= '0' && in_char <= '9')
   {
      tempval += (in_char - '0');
   }
   if(in_char >= 'A' && in_char <= 'F')
   {
      tempval += ((in_char - 'A')+10);
   }
   return tempval;
}




void load_intelhexfile(WDC65C816_Regs *regs, const char *filename)
{
   FILE *fp;
   uint16_t address;
   uint8_t rec_length;
   uint8_t checksum;
   uint8_t tempval;
   uint32_t full_address = 0;
   shuint32 temp_address;
   uint16_t load_counter = 0;
   //int line_num = SIDEWIN_MIDDLE_LOC+2;
   int line_num = SIDEWIN_MIDDLE_LOC+2;;
   mvwprintw(sidewin,line_num,1,"Intel HEX file loader: %s",filename);
   line_num++;

   fp = fopen(filename,"r");
   if(!fp)
   {
      mvwprintw(sidewin,line_num,1,"Could not find file");
      line_num++;
      return;
   }

   while((char) (tempval = fgetc(fp)) != EOF)
   { 
      if(tempval != ':')
         continue;
      address = 0;
      checksum = 0;
      rec_length = fetch_hex_number(fp);   //Get record length in bytes
      checksum += rec_length;
      //wprintw(mainwin,"Fetched value: %02X\n",rec_length);
      //wprintw(mainwin,"Fetched checksum: %02X\n",checksum);
      tempval = fetch_hex_number(fp);      //Get beginning address
      checksum += tempval;
      //wprintw(mainwin,"Fetched value: %02X\n",tempval);
      //wprintw(mainwin,"Fetched checksum: %02X\n",checksum);
      address = tempval; 
      tempval = fetch_hex_number(fp);
      checksum += tempval;
      //wprintw(mainwin,"Fetched value: %02X\n",tempval);
      //wprintw(mainwin,"Fetched checksum: %02X\n",checksum);
      address = (address << 8) + tempval;
      tempval = fetch_hex_number(fp);      //Check record type for 00 only
      checksum += tempval;
      //wprintw(mainwin,"Fetched value: %02X\n",tempval);
      //wprintw(mainwin,"Fetched checksum: %02X\n",checksum);
      if(tempval == 0)
      {
         while(rec_length > 0)
         {
            tempval = fetch_hex_number(fp);      //Fetch and write bytes in data section for size of record
            checksum += tempval;
            //wprintw(mainwin,"Fetched value: %02X\n",tempval);
            //wprintw(mainwin,"Fetched checksum: %02X\n",checksum);
            temp_address.dword = (full_address + (uint32_t) address);
            write_byte_65816(temp_address.byte[NATIVE_BYTE_UPPER], temp_address.word[NATIVE_WORD_LOW], tempval);
            address++;
            rec_length--;
            load_counter++;
         }
      }
      else if(tempval == 2)  //Set the segment address
      {
         if(rec_length != 2)
         { 
            mvwprintw(sidewin,line_num,1,"!Error in load, extended address record needs 2 bytes\n"); line_num++;
            return;
         }
         tempval = fetch_hex_number(fp);      //Throw away top byte for bank address
         checksum += tempval;
         full_address = ((uint32_t) tempval << 8);
         tempval = fetch_hex_number(fp);      //This is the byte to use for bank address
         checksum += tempval;
         full_address = ((((uint32_t) tempval) + full_address) * 16);
      }
      else if(tempval == 4)  //Set the extended address
      {
         if(rec_length != 2)
         { 
            mvwprintw(sidewin,line_num,1,"!Error in load, extended address record needs 2 bytes\n"); line_num++;
            return;
         }
         tempval = fetch_hex_number(fp);      //Throw away top byte for bank address
         checksum += tempval;
         full_address = ((uint32_t) tempval << 24);
         tempval = fetch_hex_number(fp);      //This is the byte to use for bank address
         checksum += tempval;
         full_address = ((uint32_t) tempval << 16) + full_address;
      }
      else if(tempval == 3)  //Set the segment start address
      {
         if(rec_length != 4)
         { 
            mvwprintw(sidewin,line_num,1,"!Error in load, extended address record needs 2 bytes\n"); line_num++;
            return;
         }
         tempval = fetch_hex_number(fp);      //Throw away top byte for bank address
         checksum += tempval;
         full_address = ((uint32_t) tempval << 8);
         tempval = fetch_hex_number(fp);      //This is the byte to use for bank address
         checksum += tempval;
         reg_PC = ((((uint32_t) tempval) + full_address) * 16);
         tempval = fetch_hex_number(fp);      //Throw away top byte for bank address
         checksum += tempval;
         full_address = ((uint32_t) tempval << 8);
         tempval = fetch_hex_number(fp);      //This is the byte to use for bank address
         checksum += tempval;
         reg_PC = ((((uint32_t) tempval) + full_address) + reg_PC);
      }

      else if(tempval == 5)  //Set the extended start address
      {
         if(rec_length != 4)
         { 
            mvwprintw(sidewin,line_num,1,"!Error in load, extended address record needs 2 bytes\n"); line_num++;
            return;
         }
         tempval = fetch_hex_number(fp);      //Throw away top byte for bank address
         checksum += tempval;
         reg_PC = ((uint32_t) tempval << 24);
         tempval = fetch_hex_number(fp);      //This is the byte to use for bank address
         checksum += tempval;
         reg_PB = tempval;
         tempval = fetch_hex_number(fp);      //This is the byte to use for bank address
         checksum += tempval;
         regs->PC.byte[NATIVE_BYTE_HIGH] = tempval;
         tempval = fetch_hex_number(fp);      //This is the byte to use for bank address
         checksum += tempval;
         regs->PC.byte[NATIVE_BYTE_LOW] = tempval;
      }
      else
      {
         break;
      }
      tempval = fetch_hex_number(fp);      //Get checksum value
      if(tempval != (-(checksum) & 0xFF))
      {
         mvwprintw(sidewin,line_num,1,"!Error in load, checksum did not match\n"); line_num++;
         mvwprintw(sidewin,line_num,1,"Fetched checksum: %02X \n",tempval); line_num++;
         mvwprintw(sidewin,line_num,1,"Calculated checksum: %02X \n",(-(checksum) & 0xFF)); line_num++;
         return;
      } 
      //wprintw(mainwin,"@");
   }

   fclose(fp);
   mvwprintw(sidewin,line_num,1,"...Loaded %d bytes",load_counter);  line_num++;
   mvwprintw(sidewin,line_num,1,"Finished loading HEX file");
   //wrefresh(sidewin);
}




/*
int main()
{
   char buffer1[IN_BUF_SIZE]="loadfile 0 simple.bin";
   char tok_buf[OUT_TOK_SIZE];

   if(parsetoken(buffer1,IN_BUF_SIZE," ",tok_buf,OUT_TOK_SIZE))
      wprintw(mainwin,"Token: %s\n",tok_buf);

   if(parsetoken(NULL,IN_BUF_SIZE," ",tok_buf,OUT_TOK_SIZE))
      wprintw(mainwin,"Token: %s\n",tok_buf);

   if(parsetoken(NULL,IN_BUF_SIZE," ",tok_buf,OUT_TOK_SIZE))
      wprintw(mainwin,"Token: %s\n",tok_buf);

   return 0;
}
*/

void print_registers(WDC65C816_Regs *regs)
{
   const uint8_t line_start = 3;
   uint16_t tempvalue = 0;
   //wprintw(mainwin,"View registers:\n");

   mvwprintw(sidewin,line_start,1,"Flags: NVMXDIZC E\n");
   mvwprintw(sidewin,line_start+1,1,"       ");
   tempvalue = reg_status;
   for(int i = 0; i < 8; i++)
   {
      if((tempvalue & 0x80) == 0x80)
         waddch(sidewin,'1');
      else
         waddch(sidewin,'0');
      tempvalue = tempvalue << 1;
   }
   waddch(sidewin,' ');
   if((reg_e_flag & 0x1) == 0x1)
      waddch(sidewin,'1');
   else
      waddch(sidewin,'0');
   


   mvwprintw(sidewin,line_start+3,1,"PC = %02X:%04X (%u)          ",reg_PB,reg_PC,reg_PC);
   mvwprintw(sidewin,line_start+3,20," DB = %02X          ",reg_DB);
   mvwprintw(sidewin,line_start+5,1,"         B A");
   mvwprintw(sidewin,line_start+6,1,"    C = %04X (%u)          ",reg_C,reg_C);
   //mvwprintw(sidewin,line_start+6,20," DB = %02X (%u)          ",reg_DB,reg_DB);
   mvwprintw(sidewin,line_start+7,1,"    X = %04X (%u)          ",reg_X,reg_X);
   mvwprintw(sidewin,line_start+8,1,"    Y = %04X (%u)          ",reg_Y,reg_Y);
   mvwprintw(sidewin,line_start+9,1,"    S = %04X (%u)          ",reg_SP,reg_SP);
   mvwprintw(sidewin,line_start+10,1,"    D = %04X (%u)          ",reg_DR,reg_DR);

   wrefresh(sidewin);
}


void print_serialio_chars(uint8_t charvalue)
{  
   if(charvalue != 0x0D)  //Ignore carriage return character here in Linux
   {
      waddch(mainwin, charvalue);
   }
   wrefresh(mainwin);
}

void fetch_serialio_input(WDC65C816_Regs *regs)
{
   int tempchar;
   tempchar = getch();
   if(tempchar != ERR)
   {
      switch(tempchar)
      {
          case KEY_F(4):
             run_flag_65816 = 0;
             break;
          case KEY_F(7):
             nmi_65816(regs);
             break;
          case KEY_F(8):
             irq_65816(regs);
             run_flag_65816 = 0;
             break;
          case KEY_BACKSPACE:
             circbuf_write(&UART_Receive, '\b');
	     //irq65816();
             break;
          default:
             circbuf_write(&UART_Receive, tempchar);
             break;
      }
   } 
}


void run_65816(WDC65C816_Regs *regs)
{
   uint16_t instr_counter = 0;
   run_flag_65816 = 1;
   nodelay(stdscr, TRUE);

   clear_message_win();
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC+2,1,"CPU is running");
   wrefresh(sidewin);

   while(run_flag_65816 == 1)
   {
      step_65816(regs);
      instr_counter++;
      fetch_serialio_input(regs);
      if((reg_PC == breakpoint_addr.word[NATIVE_WORD_LOW]) && (reg_PB == breakpoint_addr.byte[NATIVE_BYTE_UPPER]))
      {
         run_flag_65816 = 0;
         //print_serialio_chars();
         mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC+2,1,"Breakpoint reached     ");
         wrefresh(sidewin);
         nodelay(stdscr, FALSE);
         return;
      }
   }
   //Finish printing all remaining chars
   //print_serialio_chars();
   clear_message_win();
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC+2,1,"CPU has stopped");
   wrefresh(sidewin);
   nodelay(stdscr, FALSE);
   breakpoint_addr.dword = 0;
}



int main()
{
   //Variable declarations
   int index;
   int input_char;
   int run_flag = 0;
   breakpoint_addr.dword = 0;
   uint32_t temp1,temp2;
   char tok_buf[OUT_TOK_SIZE];
   WDC65C816_Regs *regs;

   /*  Initialize ncurses  */
   if ( (initscr()) == NULL ) {
      //fwprintw(mainwin,mainwin,stderr, "Error initializing ncurses.\n");
      exit(EXIT_FAILURE);
   }

   noecho();                  /*  Turn off key echoing                 */
   cbreak();
   nodelay(stdscr, FALSE);
   keypad(stdscr, TRUE);     /*  Enable the keypad for non-char keys  */
   mainwin = newwin(LINES, COLS-SIDEWIN_WIDTH, 0, 0);
   sidewin = newwin(LINES, SIDEWIN_WIDTH, 0, COLS - SIDEWIN_WIDTH);
   scrollok(mainwin,TRUE);
   wrefresh(mainwin);
   wrefresh(sidewin);
   refresh();

   //Initialize all devices and peripherals
   regs = &vm_regs;
   reset_65816(regs);
   circbuf_setup(&UART_Receive,UART_Receive_Buffer,UART_BUFFER_SIZE);

   mvwprintw(sidewin,0,1,"65C816 Simulator\n");
   mvwprintw(sidewin,1,1,"Copyright 2024 by Michael Burns\n\n");
   for(index = 0; index < LINES; index++)
   {
      mvwaddch(sidewin, index, 0, '|');
   }
   for(index = 1; index < SIDEWIN_WIDTH; index++)
   {
      mvwaddch(sidewin, SIDEWIN_MIDDLE_LOC-1, index, '-');
      mvwaddch(sidewin, SIDEWIN_MIDDLE_LOC+1, index, '-');
   }
   
   /*
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-9,1,"Command Keys:");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-8,1,"F1 - Step Instruction");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-7,1,"F2 - Dump Memory");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-6,1,"F3 - Reset Processor");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-5,1,"F4 - Run processor  ");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-4,1,"F5 - Load HEX File");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-3,1,"F6 - Set Breakpoint");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-2,1,"F9 - Exit from Simulator");
   */
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-7,1,"Command Keys:");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-6,1,"F1 - Step Instruction\tF2 - Dump Memory");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-5,1,"F3 - Reset Processor\tF4 - Run processor  ");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-4,1,"F5 - Load HEX File\tF6 - Set Breakpoint");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-3,1,"F7 - Perform NMI\tF8 - Perform IRQ");
   mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-2,1,"F9 - Exit from Simulator");


   print_registers(regs);
   clear_message_win();
   clear_main_win();
   wrefresh(mainwin);
   wrefresh(sidewin);
   refresh();

   run_flag = 1;
   while(run_flag != 0)
   {
      refresh();  //For the ncurses window
      input_char = getch();
      switch(input_char)
      {
	  case KEY_F(2):
             //temp1=reg_PC;
             //temp2=reg_PC+0xFF;
             get_command_str(sidewin, SIDEWIN_MIDDLE_LOC, 1, "Start Address: ", in_buffer, 30);
             temp1=strtol(in_buffer, NULL, 16);
             temp2=temp1+0xFF;
             clear_message_win();
             dumpmem(regs,temp1,temp2);
             break;
          case KEY_F(3):
             reset_65816(regs);
             circbuf_setup(&UART_Receive,UART_Receive_Buffer,UART_BUFFER_SIZE);
	         clear_main_win();
             clear_message_win();
             mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC+2,1,"CPU has been reset");
             print_registers(regs);
    	     break;
          case KEY_F(4):
             mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-5,1,"F3 - Reset Processor\tF4 - Stop processor  ");
             run_65816(regs);
             mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC-5,1,"F3 - Reset Processor\tF4 - Run processor   ");
             print_registers(regs);
	         break;
    	  case KEY_F(1):
             step_65816(regs);  
             //check_for_interrupt();
             //print_serialio_chars(); 
             print_registers(regs);
	         break;
          case KEY_F(5):
             clear_message_win();
             get_command_str(sidewin, SIDEWIN_MIDDLE_LOC, 1, "Filename: ", in_buffer, 30);
             load_intelhexfile(regs,in_buffer);
             wrefresh(sidewin);
             break;
     	  case KEY_F(6):
             //temp1=reg_PC;
             //temp2=reg_PC+0xFF;
             get_command_str(sidewin, SIDEWIN_MIDDLE_LOC, 1, "Set Breakpoint Address(0 to clear): ", in_buffer, 30);
             temp1=strtol(in_buffer, NULL, 16);
             breakpoint_addr.dword = temp1;
             if(temp1 == 0)
                mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC+4,1,"Breakpoint Cleared     ");
             else
                mvwprintw(sidewin,SIDEWIN_MIDDLE_LOC+4,1,"Breakpoint Set: %02X:%04X   ",breakpoint_addr.byte[NATIVE_BYTE_UPPER], breakpoint_addr.word[NATIVE_WORD_LOW]);
             wrefresh(sidewin);
             break;
          case KEY_F(7):
             nmi_65816(regs);
	         break;
          case KEY_F(8):
             irq_65816(regs);
	         break;
          case KEY_F(9):
    	     run_flag = 0;
    	     break;
          default:
    	     break;
      }
      
   }

   delwin(stdscr);
   endwin();
   refresh();

   return 0;
}

