/* WDC65C02 CPU emulator core v2.0 *******************
 * (c)2021 by Michael Burns                          *
 *****************************************************
 * v2.0 - Rewrote the decode and instruction         *
 *        functions to be more efficient as well as  *
 *        using union datatypes to access individual *
 *        bytes in words instead of using slower bit *
 *        shifts.  Other changes made to improve     *
 *        efficiency (10/16/2021)                    *
 *                                                   *
 * v1.0 - Updated to emulate the WDC65C02 variant    *
 *        processor with all the new addressing modes*
 *        and opcodes that are available for the     *
 *        WDC65C02.  Also updated the exec6502 to    *
 *        give simulators greater control of the loop*
 *        (made by Michael Burns 01/15/2021)         *
 *                                                   *
 *****************************************************
 * LICENSE: This source code is released into the    *
 * public domain, but if you use it please do give   *
 * credit to me and Mike Chambers.                   *
 *                                                   *
 *****************************************************
 * WDC65C02 is a Western Design Center 65C02 CPU     *
 * emulation engine in C. It was designed to be as   *
 * efficient an emulation as possible that I could   *
 * do in the C programming language while still      *
 * being portable.                                   *
 *                                                   *
 * Based off of code from the Fake6502 CPU emulator  *
 * core v1.1 written by:                             *
 * (c)2011 Mike Chambers (miker00lz@gmail.com)       *
 *                                                   *
 *****************************************************
 * Usage:                                            *
 *                                                   *
 * WDC65C02 requires you to provide two external     *
 * functions:                                        *
 *                                                   *
 * uint8_t read_byte_65816(uint16_t address)                *
 * void write65816(uint16_t address, uint8_t value)   *
 *                                                   *
 *****************************************************
 * Useful functions in this emulator:                *
 *                                                   *
 * void reset65816()                                  *
 *   - Call this once before you begin execution.    *
 *                                                   *
 * void exec65816(uint32_t tickcount)                 *
 *   - Execute 65816 code up to the next specified    *
 *     count of clock ticks.                         *
 *                                                   *
 * void step65816()                                   *
 *   - Execute a single instrution.                  *
 *                                                   *
 * void irq65816()                                    *
 *   - Trigger a hardware IRQ in the 65816 core.      *
 *                                                   *
 * void nmi65816()                                    *
 *   - Trigger an NMI in the 65816 core.              *
 *                                                   *
 *****************************************************/

#include <stdint.h>
#include "shared.h"
#include "wdc65c816.h"


//externally supplied functions

extern uint8_t read_byte_65816(uint8_t bank, uint16_t address);
extern void write_byte_65816(uint8_t bank, uint16_t address, uint8_t value);
extern void instr_wdm_65816(WDC65C816_Regs *regs, uint8_t opcode);
extern void instr_stp_65816(WDC65C816_Regs *regs);
extern void instr_wai_65816(WDC65C816_Regs *regs);


//Function prototypes
static uint16_t sign_extend(uint8_t val);
void step_native_mode(WDC65C816_Regs *regs);
void step_emu_mode(WDC65C816_Regs *regs);


uint16_t read_word_65816(uint8_t bank, uint16_t address)
{
   shuint16 temp_value;
   temp_value.byte[NATIVE_BYTE_LOW] = read_byte_65816(bank, address);
   temp_value.byte[NATIVE_BYTE_HIGH] = read_byte_65816(bank, address+1);
   return temp_value.word;
}

static void write_word_65816(uint8_t bank, uint16_t address, uint16_t value)
{
   shuint16 temp_value;
   temp_value.word = value;
   write_byte_65816(bank, address, temp_value.byte[NATIVE_BYTE_LOW]);
   write_byte_65816(bank, address+1, temp_value.byte[NATIVE_BYTE_HIGH]);
}

//a few general functions used by various other functions
static void push_word(WDC65C816_Regs *regs, uint16_t pushval) {
   shuint16 temp_value;
   temp_value.word = pushval;
   write_byte_65816(0, reg_SP, temp_value.byte[NATIVE_BYTE_HIGH]);
   write_byte_65816(0, reg_SP-1, temp_value.byte[NATIVE_BYTE_LOW]); 
   reg_SP -= 2;
}

static void push_byte(WDC65C816_Regs *regs, uint8_t pushval) {
   write_byte_65816(0, reg_SP, pushval);
   reg_SP--;
}

static uint16_t pull_word(WDC65C816_Regs *regs) {
    shuint16 temp16;
    reg_SP++;
    temp16.byte[NATIVE_BYTE_LOW] = read_byte_65816(0,reg_SP);
    reg_SP++;
    temp16.byte[NATIVE_BYTE_HIGH] = read_byte_65816(0,reg_SP);
    return(temp16.word);
}

uint8_t pull_byte(WDC65C816_Regs *regs) {
    reg_SP++;
    return (read_byte_65816(0,reg_SP));
}

void reset_65816(WDC65C816_Regs *regs) 
{
    reg_PB = 0;
    reg_DB = 0;
    reg_PC = read_word_65816(0,WDC6502_RESET_VECTOR);
    reg_C = 0;
    reg_X = 0;
    reg_Y = 0;
    reg_SP = 0x01FF;
    reg_e_flag = FLAG_EMU;
    reg_status = (FLAG_MS | FLAG_XS);
}


static uint16_t sign_extend(uint8_t val)
{
    uint16_t res;

    res = val;
    if (val & 0x80)
        res |= 0xFF00;
    return res;
}


//instruction handler functions
static inline uint8_t instr_adc8(WDC65C816_Regs *regs, uint8_t value) 
{
    shuint16 result;
    result.word = (uint16_t)reg_A + (uint16_t) value + (uint16_t)(reg_status & FLAG_CARRY);
   
    update_flag((result.byte[NATIVE_BYTE_HIGH] != 0), FLAG_CARRY);
    zerocalc8(result.byte[NATIVE_BYTE_LOW]);
    overflowcalc8(result, reg_A, value);
    signcalc8(result.byte[NATIVE_BYTE_LOW]);
    
    if (reg_status & FLAG_DECIMAL) {
        clearcarry();
        
        if ((result.byte[NATIVE_BYTE_LOW] & 0x0F) > 0x09) {
            result.byte[NATIVE_BYTE_LOW] += 0x06;
        }
        if ((result.byte[NATIVE_BYTE_LOW] & 0xF0) > 0x90) {
            result.byte[NATIVE_BYTE_LOW] += 0x60;
            setcarry();
        }
        
    }
   
    return(result.byte[NATIVE_BYTE_LOW]);
}


static inline uint16_t instr_adc16(WDC65C816_Regs *regs, uint16_t value) {
    shuint32 result;

    result.dword = (uint32_t)reg_C + value + (uint16_t)(reg_status & FLAG_CARRY);
   
    update_flag((result.byte[NATIVE_BYTE_UPPER] != 0), FLAG_CARRY);
    zerocalc16(result.word[NATIVE_WORD_LOW]);
    overflowcalc16(result, reg_C, value);
    signcalc8(result.byte[NATIVE_BYTE_HIGH]);
    
    return(result.word[NATIVE_WORD_LOW]);    
}



static inline uint8_t instr_asl8(WDC65C816_Regs *regs, uint8_t value) {
    uint8_t result;  
   
    result = value << 1;
    update_flag(((value & 0x80) == 0x80), FLAG_CARRY);
    zerocalc8(result);
    signcalc8(result);
   
    return(result); 
}


static inline uint16_t instr_asl16(WDC65C816_Regs *regs, uint16_t value) {
    shuint16 result;  
   
    result.word = value << 1;
    update_flag(((value & 0x8000) == 0x8000), FLAG_CARRY);
    zerocalc16(result.word);
    signcalc8(result.byte[NATIVE_BYTE_HIGH]);
   
    return(result.word); 
}




static inline void instr_bit8(WDC65C816_Regs *regs, uint8_t value) 
{
    uint8_t result;

    result = reg_A & value;
    zerocalc8(result); 
    reg_status = (reg_status & 0x3F) | (uint8_t)(value & 0xC0);
}


static inline void instr_bit16(WDC65C816_Regs *regs, uint16_t value) 
{
    shuint16 result;

    result.word = reg_C & value;
    update_flag((result.word == 0),FLAG_ZERO);
    result.word = value;
    reg_status = (reg_status & 0x3F) | (uint8_t)(result.byte[NATIVE_BYTE_HIGH] & 0xC0);
}



static inline void instr_brk(WDC65C816_Regs *regs) {
    if(reg_e_flag & FLAG_EMU)
    {
       push_word(regs,reg_PC+1); //push next instruction address onto stack
       push_byte(regs,reg_status);
       reg_PB = 0;
       reg_PC = read_word_65816(0, WDC6502_IRQ_VECTOR);
    }
    else
    {
       push_byte(regs,reg_PB);
       push_word(regs,reg_PC+1); //push next instruction address onto stack
       push_byte(regs,reg_status);
       reg_PB = 0;
       reg_PC = read_word_65816(0, WDC65816_BRK_VECTOR);
    }
    setinterrupt(); //set interrupt flag
}


static inline void instr_cmp8(WDC65C816_Regs *regs, uint8_t value) {
    uint8_t result;
    
    result = reg_A - value;
    if (reg_A >= value) setcarry(); else clearcarry();
    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);
}


static inline void instr_cmp16(WDC65C816_Regs *regs, uint16_t value) {
    shuint16 result;
    result.word = reg_C - value;
   
    if(reg_C >= value) setcarry(); else clearcarry();
    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);
}



static inline void instr_cpx8(WDC65C816_Regs *regs, uint8_t value) 
{
    uint8_t result;
    
    result = reg_X_low - value;
    if (reg_X_low >= value) setcarry(); else clearcarry();
    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);
}


static inline void instr_cpx16(WDC65C816_Regs *regs, uint16_t value) 
{
    shuint16 result;
    result.word = reg_X - value;
   
    if(reg_X >= value) setcarry(); else clearcarry();
    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);
}


static inline void instr_cpy8(WDC65C816_Regs *regs, uint8_t value) 
{
    uint8_t result;
    
    result = reg_Y_low - value;
    if (reg_Y_low >= value) setcarry(); else clearcarry();
    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);
}


static inline void instr_cpy16(WDC65C816_Regs *regs, uint16_t value) 
{
    shuint16 result;
    result.word = reg_Y - value;
   
    if(reg_Y >= value) setcarry(); else clearcarry();
    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);
}



static inline uint8_t instr_dec8(WDC65C816_Regs *regs, uint8_t value) 
{
    uint8_t result;
    result = value - 1;

    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);

    return(result);   
}


static inline uint16_t instr_dec16(WDC65C816_Regs *regs, uint16_t value) 
{
    shuint16 result;
    result.word = value - 1;

    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);

    return(result.word);   
}


static inline uint8_t instr_inc8(WDC65C816_Regs *regs, uint8_t value) 
{
    uint8_t result;
    result = value + 1;

    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);

    return(result);   
}


static inline uint16_t instr_inc16(WDC65C816_Regs *regs, uint16_t value) 
{
    shuint16 result;
    result.word = value + 1;

    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);

    return(result.word);   
}



static inline uint8_t instr_lsr8(WDC65C816_Regs *regs, uint8_t value) 
{
    uint8_t result;

    update_flag(((value & 1) == 1),FLAG_CARRY);
    result = value >> 1;
    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);
    return(result);
}



static inline uint16_t instr_lsr16(WDC65C816_Regs *regs, uint16_t value) 
{
    shuint16 result;

    update_flag(((value & 1) == 1),FLAG_CARRY);
    result.word = (value >> 1);
    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);
    return(result.word);
}


static inline uint8_t instr_tsb8(WDC65C816_Regs *regs, uint8_t value) 
{
    zerocalc8((value & reg_A));
    return(value | reg_A);
}


static inline uint16_t instr_tsb16(WDC65C816_Regs *regs, uint16_t value) 
{
    zerocalc16((value & reg_C));
    return(value | reg_C);
}


static inline uint8_t instr_trb8(WDC65C816_Regs *regs, uint8_t value) 
{
    zerocalc8((value & reg_A));
    return(value & (~(reg_A)));
}


static inline uint16_t instr_trb16(WDC65C816_Regs *regs, uint16_t value) 
{
    zerocalc16((value & reg_C));
    return(value & (~(reg_C)));
}




static inline uint8_t instr_rol8(WDC65C816_Regs *regs, uint8_t value) {
    uint8_t result;
    
    result = (value << 1) | (((reg_status & FLAG_CARRY) != 0) ? 1 : 0);
    update_flag(((value & 0x80) == 0x80),FLAG_CARRY);
    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);
    return(result);
}


static inline uint16_t instr_rol16(WDC65C816_Regs *regs, uint16_t value) {
    shuint16 result;
    
    result.word = (value << 1) | (((reg_status & FLAG_CARRY) != 0) ? 1 : 0);
    update_flag(((value & 0x8000) == 0x8000),FLAG_CARRY);
    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);
    return(result.word);
}


static inline uint8_t instr_ror8(WDC65C816_Regs *regs, uint8_t value) {
    uint8_t result;
    
    result = (value >> 1) | (((reg_status & FLAG_CARRY) != 0) ? 0x80 : 0);
    update_flag(((value & 1) == 1),FLAG_CARRY);
    update_flag((result == 0),FLAG_ZERO);
    update_flag(((result & 0x80) == 0x80),FLAG_SIGN);
    return(result);
}


static inline uint16_t instr_ror16(WDC65C816_Regs *regs, uint16_t value) {
    shuint16 result;
    
    result.word = (value >> 1) | (((reg_status & FLAG_CARRY) != 0) ? 0x8000 : 0);
    update_flag(((value & 1) == 1),FLAG_CARRY);
    update_flag((result.word == 0),FLAG_ZERO);
    update_flag(((result.byte[NATIVE_BYTE_HIGH] & 0x80) == 0x80),FLAG_SIGN);
    return(result.word);
}


//####
static inline uint8_t instr_sbc8(WDC65C816_Regs *regs, uint8_t value) 
{
    shuint16 result;
    uint8_t temp;

    temp = value ^ 0x00FF;
    result.word = (uint16_t)reg_A + temp + (uint16_t)(reg_status & FLAG_CARRY);
   
    update_flag((result.byte[NATIVE_BYTE_HIGH] != 0), FLAG_CARRY);
    zerocalc8(result.byte[NATIVE_BYTE_LOW]);
    overflowcalc8(result, reg_A, temp);
    signcalc8(result.byte[NATIVE_BYTE_LOW]);
    
    if (reg_status & FLAG_DECIMAL) {
        clearcarry();

        result.byte[NATIVE_BYTE_LOW] -= 0x66;
        if ((result.byte[NATIVE_BYTE_LOW] & 0x0F) > 0x09) {
            result.byte[NATIVE_BYTE_LOW] += 0x06;
        }
        if ((result.byte[NATIVE_BYTE_LOW] & 0xF0) > 0x90) {
            result.byte[NATIVE_BYTE_LOW] += 0x60;
            setcarry();
        }
 
    }
    return(result.byte[NATIVE_BYTE_LOW]);
}


static inline uint16_t instr_sbc16(WDC65C816_Regs *regs, uint16_t value) 
{
    shuint32 result;
    result.dword = (uint32_t)reg_C + (uint32_t)(value ^ 0xFFFF) + (uint32_t)(reg_status & FLAG_CARRY);
   
    update_flag((result.byte[NATIVE_BYTE_UPPER] != 0), FLAG_CARRY);
    zerocalc16(result.word[NATIVE_WORD_LOW]);
    overflowcalc16(result, reg_C, (value ^ 0xFFFF));
    signcalc8(result.byte[NATIVE_BYTE_HIGH]);
    
    return(result.word[NATIVE_WORD_LOW]);
}




static inline void instr_mvn(WDC65C816_Regs *regs, uint8_t srcbank, uint8_t destbank)
{
   reg_DB = destbank;

   while(reg_C != 0xFFFF)
   {
      write_byte_65816(reg_DB, reg_Y, read_byte_65816(srcbank, reg_X));
      reg_X++;
      reg_Y++;
      reg_C--; 
   }
}

static inline void instr_mvp(WDC65C816_Regs *regs, uint8_t srcbank, uint8_t destbank)
{
   reg_DB = destbank;

   while(reg_C != 0xFFFF)
   {
      write_byte_65816(reg_DB, reg_Y, read_byte_65816(srcbank, reg_X));
      reg_X--;
      reg_Y--;
      reg_C--; 
   }
}


static inline void instr_xce(WDC65C816_Regs *regs)
{
   if (reg_status & FLAG_CARRY) 
   { 
      update_flag(((reg_e_flag & FLAG_EMU) == FLAG_EMU),FLAG_CARRY);
      reg_e_flag |= FLAG_EMU;
      reg_SP_high = 0x1;
      //reg_status &= (~(FLAG_MS | FLAG_XS)); 
   }
   else
   {
      update_flag(((reg_e_flag & FLAG_EMU) == FLAG_EMU),FLAG_CARRY);
      reg_e_flag &= (~(FLAG_EMU));
      //reg_status |= (FLAG_MS | FLAG_XS);  
   }
   reg_status |= (FLAG_MS | FLAG_XS);  
}


void nmi_65816(WDC65C816_Regs *regs) {
    interrupt_status |= FLAG_NMI;
}

void irq_65816(WDC65C816_Regs *regs) {
    interrupt_status |= FLAG_INTERRUPT;
}


void perform_emu_interrupt(WDC65C816_Regs *regs)
{
    if((interrupt_status & FLAG_NMI) == FLAG_NMI)
    {
       push_word(regs,reg_PC); //push next instruction address onto stack
       push_byte(regs,reg_status);
       reg_PB = 0;
       reg_PC = read_word_65816(0, WDC6502_NMI_VECTOR);
       setinterrupt(); //set interrupt flag
       interrupt_status &= ~(FLAG_NMI);
       return;
    }

    if((interrupt_status & FLAG_INTERRUPT & (~(reg_status))) == FLAG_INTERRUPT)
    {
       push_word(regs,reg_PC); //push next instruction address onto stack
       push_byte(regs,reg_status);
       reg_PB = 0;
       reg_PC = read_word_65816(0, WDC6502_IRQ_VECTOR);
       setinterrupt(); //set interrupt flag
       interrupt_status &= ~(FLAG_INTERRUPT);
       return;
    }
    return;
}


void perform_native_interrupt(WDC65C816_Regs *regs)
{
    if((interrupt_status & FLAG_NMI) == FLAG_NMI)
    {
       push_byte(regs,reg_PB);
       push_word(regs,reg_PC); //push next instruction address onto stack
       push_byte(regs,reg_status);
       reg_PB = 0;
       reg_PC = read_word_65816(0, WDC65816_NMI_VECTOR);
       setinterrupt(); //set interrupt flag
       interrupt_status &= ~(FLAG_NMI);
       return;
    }

    if((interrupt_status & FLAG_INTERRUPT & (~(reg_status))) == FLAG_INTERRUPT)
    {
       push_byte(regs,reg_PB);
       push_word(regs,reg_PC); //push next instruction address onto stack
       push_byte(regs,reg_status);
       reg_PB = 0;
       reg_PC = read_word_65816(0, WDC65816_IRQ_VECTOR);
       setinterrupt(); //set interrupt flag
       interrupt_status &= ~(FLAG_INTERRUPT);
       return;
    }
    return;
}


void step_65816(WDC65C816_Regs *regs) 
{
    if(reg_e_flag & FLAG_EMU)
    {
       step_emu_mode(regs);
    }
    else
    {
       step_native_mode(regs);
    }
}


void step_emu_mode(WDC65C816_Regs *regs) 
{
    uint8_t opcode;
    shuint32 effective_address;

    if(interrupt_status != 0)
    {
       perform_emu_interrupt(regs);
    }

    opcode = read_byte_65816(reg_PB, reg_PC++);

    switch(opcode)
    {
       case 0x00:  //brk_imp
         push_word(regs,reg_PC+1); push_byte(regs,reg_status | FLAG_BREAK); reg_PB = 0; reg_PC = read_word_65816(0, WDC6502_IRQ_VECTOR); setinterrupt();
         break;
       case 0x01:  //ora_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x02:  //cop_imm8
         push_word(regs,reg_PC+1); push_byte(regs,reg_status); reg_PB = 0; reg_PC = read_word_65816(0, WDC6502_COP_VECTOR); setinterrupt();
         break;
       case 0x03:  //ora_ds
         reg_A = reg_A | read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x04:  //tsb_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_tsb8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x05:  //ora_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_A = reg_A | read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x06:  //asl_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_asl8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x07:  //ora_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x08:  //php_imp
         push_byte(regs, reg_status);
         break;
       case 0x09:  //ora_imma
         reg_A = reg_A | read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x0A:  //asl_acc
         reg_A = instr_asl8(regs, reg_A);
         break;
       case 0x0B:  //phd_imp
         push_word(regs, reg_DR);
         break;
       case 0x0C:  //tsb_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_tsb8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0x0D:  //ora_abso
         reg_A = reg_A | read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);
         reg_PC+=2;
         break;
       case 0x0E:  //asl_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_asl8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0x0F:  //ora_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x10:  //bpl_rel
         if ((reg_status & FLAG_SIGN) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x11:  //ora_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x12:  //ora_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x13:  //ora_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x14:  //trb_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_trb8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x15:  //ora_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         reg_A = reg_A | read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x16:  //asl_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_asl8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x17:  //ora_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x18:  //clc_imp
         clearcarry();
         break;
       case 0x19:  //ora_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x1A:  //inc_acc
         reg_A++; zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x1B:  //tcs_imp
         reg_SP = reg_C;
         break;
       case 0x1C:  //trb_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_trb8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0x1D:  //ora_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x1E:  //asl_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, instr_asl8(regs, read_byte_65816(reg_DB, effaddr_word)));
         break;
       case 0x1F:  //ora_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x20:  //jsr_abso
         push_word(regs ,reg_PC+1); reg_PC = read_word_65816(reg_PB, reg_PC);
         break;
       case 0x21:  //and_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x22:  //jsl_absl
         push_byte(regs, reg_PB); push_word(regs ,reg_PC+2); effaddr_bank = read_byte_65816(reg_PB, reg_PC+2); reg_PC = read_word_65816(reg_PB, reg_PC); reg_PB = effaddr_bank;
         break;
       case 0x23:  //and_ds
         reg_A = reg_A & read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x24:  //bit_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         instr_bit8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0x25:  //and_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_A = reg_A & read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x26:  //rol_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_rol8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x27:  //and_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x28:  //plp_imp
         reg_status = (pull_byte(regs) & (~(FLAG_MS | FLAG_XS))); 
         break;
       case 0x29:  //and_imma
         reg_A = reg_A & read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x2A:  //rol_acc
         reg_A = instr_rol8(regs, reg_A);
         break;
       case 0x2B:  //pld_imp
         reg_DR = pull_word(regs); zerocalc16(reg_DR); signcalc8(reg_DR_high);
         break;
       case 0x2C:  //bit_abso
         instr_bit8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));
         reg_PC+=2;
         break;
       case 0x2D:  //and_abso
         reg_A = reg_A & read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);
         reg_PC+=2;
         break;
       case 0x2E:  //rol_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_rol8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0x2F:  //and_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x30:  //bmi_rel
         if ((reg_status & FLAG_SIGN) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x31:  //and_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x32:  //and_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x33:  //and_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x34:  //bit_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         instr_bit8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0x35:  //and_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         reg_A = reg_A & read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x36:  //rol_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_rol8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x37:  //and_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x38:  //sec_imp
         setcarry();
         break;
       case 0x39:  //and_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x3A:  //dec_acc
         reg_A--; zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x3B:  //tsc_imp
         reg_C = reg_SP; zerocalc16(reg_C); zerocalc8(reg_B);
         break;
       case 0x3C:  //bit_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         instr_bit8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0x3D:  //and_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x3E:  //rol_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, instr_rol8(regs, read_byte_65816(reg_DB, effaddr_word)));
         break;
       case 0x3F:  //and_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x40:  //rti_imp
         reg_status = (pull_byte(regs) & (~(FLAG_MS | FLAG_XS))); reg_PC = pull_word(regs);
         break;
       case 0x41:  //eor_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x42:  //wdm_imm8
         instr_wdm_65816(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0x43:  //eor_ds
         reg_A = reg_A ^ read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x44:  //mvp_abso
         instr_mvp(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)), read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)+1));
         reg_PC+=2;
         break;
       case 0x45:  //eor_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_A = reg_A ^ read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x46:  //lsr_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_lsr8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x47:  //eor_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x48:  //pha_imp
         push_byte(regs, reg_A);
         break;
       case 0x49:  //eor_imma
         reg_A = reg_A ^ read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x4A:  //lsr_acc
         reg_A = instr_lsr8(regs, reg_A);
         break;
       case 0x4B:  //phk_imp
         push_byte(regs, reg_PB);
         break;
       case 0x4C:  //jmp_abso
         reg_PC = read_word_65816(reg_PB, reg_PC);
         break;
       case 0x4D:  //eor_abso
         reg_A = reg_A ^ read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);
         reg_PC+=2;
         break;
       case 0x4E:  //lsr_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_lsr8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0x4F:  //eor_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x50:  //bvc_rel
         if ((reg_status & FLAG_OVERFLOW) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x51:  //eor_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x52:  //eor_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x53:  //eor_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x54:  //mvn_abso
         instr_mvn(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)), read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)+1));
         reg_PC+=2;
         break;
       case 0x55:  //eor_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         reg_A = reg_A ^ read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x56:  //lsr_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_lsr8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x57:  //eor_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x58:  //cli_imp
         clearinterrupt();
         break;
       case 0x59:  //eor_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x5A:  //phy_imp
         push_byte(regs, reg_Y_low);
         break;
       case 0x5B:  //tcd_imp
         reg_DR = reg_C; zerocalc16(reg_DR); zerocalc8(reg_DR_high);
         break;
       case 0x5C:  //jml_absl
         effaddr_bank = read_byte_65816(reg_PB, reg_PC+2); reg_PC = read_word_65816(reg_PB, reg_PC); reg_PB = effaddr_bank;
         break;
       case 0x5D:  //eor_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x5E:  //lsr_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, instr_lsr8(regs, read_byte_65816(reg_DB, effaddr_word)));
         break;
       case 0x5F:  //eor_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x60:  //rts_imp
         reg_PC = (pull_word(regs)+1);
         break;
       case 0x61:  //adc_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0x62:  //per_rell
         push_word(regs, reg_PC+read_word_65816(reg_PB, reg_PC)); reg_PC+=2;
         break;
       case 0x63:  //adc_ds
         reg_A = instr_adc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));
         break;
       case 0x64:  //stz_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, 0);
         break;
       case 0x65:  //adc_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_A = instr_adc8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0x66:  //ror_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_ror8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x67:  //adc_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0x68:  //pla_imp
         reg_A = pull_byte(regs); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x69:  //adc_imma
         reg_A = instr_adc8(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0x6A:  //ror_acc
         reg_A = instr_ror8(regs, reg_A);
         break;
       case 0x6B:  //rtl_imp
         reg_PC = (pull_word(regs)+1); reg_PB = pull_byte(regs);
         break;
       case 0x6C:  //jmp_abind
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         reg_PC = read_word_65816(reg_PB, effaddr_word);
         break;
       case 0x6D:  //adc_abso
         reg_A = instr_adc8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));
         reg_PC+=2;
         break;
       case 0x6E:  //ror_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_ror8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0x6F:  //adc_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0x70:  //bvs_rel
         if ((reg_status & FLAG_OVERFLOW) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x71:  //adc_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0x72:  //adc_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0x73:  //adc_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0x74:  //stz_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, 0);
         break;
       case 0x75:  //adc_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         reg_A = instr_adc8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0x76:  //ror_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_ror8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0x77:  //adc_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0x78:  //sei_imp
         setinterrupt();
         break;
       case 0x79:  //adc_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0x7A:  //ply_imp
         reg_Y = pull_byte(regs); zerocalc8(reg_Y); signcalc8(reg_Y);
         break;
       case 0x7B:  //tdc_imp
         reg_C = reg_DR; zerocalc16(reg_C); zerocalc8(reg_B);
         break;
       case 0x7C:  //jmp_abindx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         reg_PC = read_word_65816(reg_PB, effaddr_word + (uint16_t) reg_X_low);
         break;
       case 0x7D:  //adc_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0x7E:  //ror_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, instr_ror8(regs, read_byte_65816(reg_DB, effaddr_word)));
         break;
       case 0x7F:  //adc_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0x80:  //bra_rel
         reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1;
         break;
       case 0x81:  //sta_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         write_byte_65816(reg_DB, effaddr_word, reg_A);
         break;
       case 0x82:  //brl_rell
         reg_PC += read_word_65816(reg_PB, reg_PC) + 1;
         break;
       case 0x83:  //sta_ds
         write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP, reg_A);
         break;
       case 0x84:  //sty_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, reg_Y_low);
         break;
       case 0x85:  //sta_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, reg_A);
         break;
       case 0x86:  //stx_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, reg_X_low);
         break;
       case 0x87:  //sta_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         write_byte_65816(effaddr_bank, effaddr_word, reg_A);
         break;
       case 0x88:  //dey_imp
         reg_Y_low--; zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         break;
       case 0x89:  //bit_imma
         instr_bit8(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0x8A:  //txa_imp
         reg_A = reg_X_low; zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x8B:  //phb_imp
         push_byte(regs, reg_DB);
         break;
       case 0x8C:  //sty_abso
         write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_Y_low);
         reg_PC+=2;
         break;
       case 0x8D:  //sta_abso
         write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_A);
         reg_PC+=2;
         break;
       case 0x8E:  //stx_abso
         write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_X_low);
         reg_PC+=2;
         break;
       case 0x8F:  //sta_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         write_byte_65816(effaddr_bank, effaddr_word, reg_A);
         break;
       case 0x90:  //bcc_rel
         if ((reg_status & FLAG_CARRY) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x91:  //sta_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         write_byte_65816(reg_DB, effaddr_word, reg_A);
         break;
       case 0x92:  //sta_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         write_byte_65816(reg_DB, effaddr_word, reg_A);
         break;
       case 0x93:  //sta_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         write_byte_65816(reg_DB, effaddr_word, reg_A);
         break;
       case 0x94:  //sty_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, reg_Y_low);
         break;
       case 0x95:  //sta_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, reg_A);
         break;
       case 0x96:  //stx_zpy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_Y_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, reg_X_low);
         break;
       case 0x97:  //sta_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         write_byte_65816(effaddr_bank, effaddr_word, reg_A);
         break;
       case 0x98:  //tya_imp
         reg_A = reg_Y_low; zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0x99:  //sta_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, reg_A);
         break;
       case 0x9A:  //txs_imp
         reg_SP_low = reg_X_low; reg_SP_high = 1;
         break;
       case 0x9B:  //txy_imp
         reg_Y_low = reg_X_low; zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         break;
       case 0x9C:  //stz_abso
         write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), 0);
         reg_PC+=2;
         break;
       case 0x9D:  //sta_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, reg_A);
         break;
       case 0x9E:  //stz_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, 0);
         break;
       case 0x9F:  //sta_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         write_byte_65816(effaddr_bank, effaddr_word, reg_A);
         break;
       case 0xA0:  //ldy_immx
         reg_Y_low = read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         break;
       case 0xA1:  //lda_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xA2:  //ldx_immx
         reg_X_low = read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_X_low); signcalc8(reg_X_low);
         break;
       case 0xA3:  //lda_ds
         reg_A = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xA4:  //ldy_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_Y_low = read_byte_65816(0, effaddr_word); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         break;
       case 0xA5:  //lda_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_A = read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xA6:  //ldx_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_X_low = read_byte_65816(0, effaddr_word); zerocalc8(reg_X_low); signcalc8(reg_X_low);
         break;
       case 0xA7:  //lda_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xA8:  //tay_imp
         reg_Y_low = reg_A; zerocalc8(reg_Y); signcalc8(reg_Y);
         break;
       case 0xA9:  //lda_imma
         reg_A = read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xAA:  //tax_imp
         reg_X_low = reg_A; zerocalc8(reg_X); signcalc8(reg_X);
         break;
       case 0xAB:  //plb_imp
         reg_DB = pull_byte(regs); zerocalc8(reg_DB); signcalc8(reg_DB);
         break;
       case 0xAC:  //ldy_abso
         reg_Y_low = read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         reg_PC+=2;
         break;
       case 0xAD:  //lda_abso
         reg_A = read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);
         reg_PC+=2;
         break;
       case 0xAE:  //ldx_abso
         reg_X_low = read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_X_low); signcalc8(reg_X_low);
         reg_PC+=2;
         break;
       case 0xAF:  //lda_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xB0:  //bcs_rel
         if ((reg_status & FLAG_CARRY) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0xB1:  //lda_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xB2:  //lda_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xB3:  //lda_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xB4:  //ldy_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         reg_Y_low = read_byte_65816(0, effaddr_word); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         break;
       case 0xB5:  //lda_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         reg_A = read_byte_65816(0, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xB6:  //ldx_zpy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_Y_low;
         effaddr_high = reg_DR_high;
         reg_X_low = read_byte_65816(0, effaddr_word); zerocalc8(reg_X_low); signcalc8(reg_X_low);
         break;
       case 0xB7:  //lda_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xB8:  //clv_imp
         clearoverflow();
         break;
       case 0xB9:  //lda_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xBA:  //tsx_imp
         reg_X_low = reg_SP_low; zerocalc8(reg_X); signcalc8(reg_X);
         break;
       case 0xBB:  //tyx_imp
         reg_X_low = reg_Y_low; zerocalc8(reg_X_low); signcalc8(reg_X_low);
         break;
       case 0xBC:  //ldy_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         reg_Y_low = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         break;
       case 0xBD:  //lda_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xBE:  //ldx_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         reg_X_low = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_X_low); signcalc8(reg_X_low);
         break;
       case 0xBF:  //lda_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xC0:  //cpy_immx
         instr_cpy8(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0xC1:  //cmp_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xC2:  //rep_imm8
         reg_status &= (~(read_byte_65816(reg_PB, reg_PC++)));
         break;
       case 0xC3:  //cmp_ds
         instr_cmp8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));
         break;
       case 0xC4:  //cpy_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         instr_cpy8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0xC5:  //cmp_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         instr_cmp8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0xC6:  //dec_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_dec8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0xC7:  //cmp_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0xC8:  //iny_imp
         reg_Y_low++; zerocalc8(reg_Y_low); signcalc8(reg_Y_low);
         break;
       case 0xC9:  //cmp_imma
         instr_cmp8(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0xCA:  //dex_imp
         reg_X_low--; zerocalc8(reg_X_low); signcalc8(reg_X_low);
         break;
       case 0xCB:  //wai_imp
         instr_wai_65816(regs);
         break;
       case 0xCC:  //cpy_abso
         instr_cpy8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));
         reg_PC+=2;
         break;
       case 0xCD:  //cmp_abso
         instr_cmp8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));
         reg_PC+=2;
         break;
       case 0xCE:  //dec_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_dec8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0xCF:  //cmp_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0xD0:  //bne_rel
         if ((reg_status & FLAG_ZERO) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0xD1:  //cmp_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xD2:  //cmp_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xD3:  //cmp_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xD4:  //pei_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         push_word(regs, effaddr_word); //%s
         break;
       case 0xD5:  //cmp_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         instr_cmp8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0xD6:  //dec_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_dec8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0xD7:  //cmp_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0xD8:  //cld_imp
         cleardecimal();
         break;
       case 0xD9:  //cmp_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xDA:  //phx_imp
         push_byte(regs, reg_X_low);
         break;
       case 0xDB:  //stp_imp
         instr_stp_65816(regs);
         break;
       case 0xDC:  //jml_abindl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_PB = effaddr_bank; reg_PC = effaddr_word;
         break;
       case 0xDD:  //cmp_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xDE:  //dec_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, instr_dec8(regs, read_byte_65816(reg_DB, effaddr_word)));
         break;
       case 0xDF:  //cmp_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0xE0:  //cpx_immx
         instr_cpx8(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0xE1:  //sbc_indx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xE2:  //sep_imm8
         reg_status |= read_byte_65816(reg_PB, reg_PC++);
         break;
       case 0xE3:  //sbc_ds
         reg_A = instr_sbc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));
         break;
       case 0xE4:  //cpx_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         instr_cpx8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0xE5:  //sbc_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         reg_A = instr_sbc8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0xE6:  //inc_zp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_inc8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0xE7:  //sbc_indl
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0xE8:  //inx_imp
         reg_X_low++; zerocalc8(reg_X_low); signcalc8(reg_X_low);
         break;
       case 0xE9:  //sbc_imma
         reg_A = instr_sbc8(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0xEA:  //nop_imp
         //*** Unimplemented ***
         break;
       case 0xEB:  //xba_imp
         effaddr_low = reg_A; reg_A = reg_B; reg_B = effaddr_low; zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xEC:  //cpx_abso
         instr_cpx8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));
         reg_PC+=2;
         break;
       case 0xED:  //sbc_abso
         reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));
         reg_PC+=2;
         break;
       case 0xEE:  //inc_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         write_byte_65816(reg_DB, effaddr_word, instr_inc8(regs, read_byte_65816(reg_DB, effaddr_word)));
         reg_PC+=2;
         break;
       case 0xEF:  //sbc_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0xF0:  //beq_rel
         if ((reg_status & FLAG_ZERO) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0xF1:  //sbc_indy
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word) + (uint16_t) reg_Y_low;
         reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xF2:  //sbc_inzp
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xF3:  //sbc_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_word = read_word_65816(0, effaddr_word) + reg_Y_low;
         reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xF4:  //pea_abso
         push_word(regs, read_word_65816(reg_PB, reg_PC)); 
         reg_PC+=2;
         break;
       case 0xF5:  //sbc_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         reg_A = instr_sbc8(regs, read_byte_65816(0, effaddr_word));
         break;
       case 0xF6:  //inc_zpx
         effaddr_low = read_byte_65816(reg_PB, reg_PC++) + reg_X_low;
         effaddr_high = reg_DR_high;
         write_byte_65816(0, effaddr_word, instr_inc8(regs, read_byte_65816(0, effaddr_word)));
         break;
       case 0xF7:  //sbc_indly
         effaddr_low = read_byte_65816(reg_PB, reg_PC++);
         effaddr_high = reg_DR_high;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y_low;
         reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       case 0xF8:  //sed_imp
         setdecimal();
         break;
       case 0xF9:  //sbc_absy
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_Y_low);
         reg_PC+=2;
         reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xFA:  //plx_imp
         reg_X = pull_byte(regs); zerocalc8(reg_X); signcalc8(reg_X);
         break;
       case 0xFB:  //xce_imp
         instr_xce(regs);
         return;
       case 0xFC:  //jsr_abindx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         push_word(regs ,reg_PC+1); reg_PC = read_word_65816(reg_PB, reg_PC);
         break;
       case 0xFD:  //sbc_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));
         break;
       case 0xFE:  //inc_absx
         effaddr_word = (read_word_65816(reg_PB, reg_PC) + reg_X_low);
         reg_PC+=2;
         write_byte_65816(reg_DB, effaddr_word, instr_inc8(regs, read_byte_65816(reg_DB, effaddr_word)));
         break;
       case 0xFF:  //sbc_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));
         break;
       default:
         return;
    }
}


void step_native_mode(WDC65C816_Regs *regs) 
{
    uint8_t opcode;
    shuint32 effective_address;

    if(interrupt_status != 0)
    {
       perform_native_interrupt(regs);
    }

    opcode = read_byte_65816(reg_PB, reg_PC++);

    switch(opcode)
    {
       case 0x00:  //brk_imp
         instr_brk(regs);
         break;
       case 0x01:  //ora_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x02:  //cop_imm8
         push_byte(regs,reg_PB); push_word(regs,reg_PC+1); push_byte(regs,reg_status); reg_PB = 0; reg_PC = read_word_65816(0, WDC65816_COP_VECTOR); setinterrupt();
         break;
       case 0x03:  //ora_ds
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x04:  //tsb_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_tsb8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_tsb16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x05:  //ora_zp
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x06:  //asl_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_asl8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_asl16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x07:  //ora_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x08:  //php_imp
         push_byte(regs, reg_status);
         break;
       case 0x09:  //ora_imma
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(reg_PB, reg_PC); zerocalc16(reg_C); signcalc8(reg_B); reg_PC+=2;}
         break;
       case 0x0A:  //asl_acc
         if(reg_status & FLAG_MS) {reg_A = instr_asl8(regs, reg_A);} else {reg_C = instr_asl16(regs, reg_C);}
         break;
       case 0x0B:  //phd_imp
         push_word(regs, reg_DR);
         break;
       case 0x0C:  //tsb_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_tsb8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_tsb16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0x0D:  //ora_abso
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc16(reg_C); signcalc8(reg_B);}
         reg_PC+=2;
         break;
       case 0x0E:  //asl_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_asl8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_asl16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0x0F:  //ora_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x10:  //bpl_rel
         if ((reg_status & FLAG_SIGN) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x11:  //ora_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x12:  //ora_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x13:  //ora_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x14:  //trb_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_trb8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_trb16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x15:  //ora_zpx
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x16:  //asl_zpx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_asl8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_asl16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x17:  //ora_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x18:  //clc_imp
         clearcarry();
         break;
       case 0x19:  //ora_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x1A:  //inc_acc
         if(reg_status & FLAG_MS) {reg_A++; zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C++; zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x1B:  //tcs_imp
         reg_SP = reg_C;
         break;
       case 0x1C:  //trb_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_trb8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_trb16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0x1D:  //ora_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x1E:  //asl_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, instr_asl8(regs, read_byte_65816(effaddr_bank, effaddr_word)));} else {write_word_65816(effaddr_bank, effaddr_word, instr_asl16(regs, read_word_65816(effaddr_bank, effaddr_word)));}
         break;
       case 0x1F:  //ora_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {reg_A = reg_A | read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C | read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x20:  //jsr_abso
         push_word(regs ,reg_PC+1); reg_PC = read_word_65816(reg_PB, reg_PC);
         break;
       case 0x21:  //and_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x22:  //jsl_absl
         push_byte(regs, reg_PB); push_word(regs ,reg_PC+2); effaddr_bank = read_byte_65816(reg_PB, reg_PC+2); reg_PC = read_word_65816(reg_PB, reg_PC); reg_PB = effaddr_bank;
         break;
       case 0x23:  //and_ds
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x24:  //bit_zp
         if(reg_status & FLAG_MS) {instr_bit8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));} else {instr_bit16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));}
         break;
       case 0x25:  //and_zp
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x26:  //rol_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_rol8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_rol16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x27:  //and_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x28:  //plp_imp
         reg_status = pull_byte(regs);
         break;
       case 0x29:  //and_imma
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(reg_PB, reg_PC); zerocalc16(reg_C); signcalc8(reg_B); reg_PC+=2;}
         break;
       case 0x2A:  //rol_acc
         if(reg_status & FLAG_MS) {reg_A = instr_rol8(regs, reg_A);} else {reg_C = instr_rol16(regs, reg_C);}
         break;
       case 0x2B:  //pld_imp
         reg_DR = pull_word(regs); zerocalc16(reg_DR); signcalc8(reg_DR_high);
         break;
       case 0x2C:  //bit_abso
         if(reg_status & FLAG_MS) {instr_bit8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));} else {instr_bit16(regs, read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));}
         reg_PC+=2;
         break;
       case 0x2D:  //and_abso
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc16(reg_C); signcalc8(reg_B);}
         reg_PC+=2;
         break;
       case 0x2E:  //rol_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_rol8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_rol16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0x2F:  //and_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x30:  //bmi_rel
         if ((reg_status & FLAG_SIGN) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x31:  //and_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x32:  //and_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x33:  //and_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x34:  //bit_zpx
         if(reg_status & FLAG_MS) {instr_bit8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));} else {instr_bit16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));}
         break;
       case 0x35:  //and_zpx
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x36:  //rol_zpx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_rol8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_rol16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x37:  //and_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x38:  //sec_imp
         setcarry();
         break;
       case 0x39:  //and_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x3A:  //dec_acc
         if(reg_status & FLAG_MS) {reg_A--; zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C--; zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x3B:  //tsc_imp
         reg_C = reg_SP; zerocalc16(reg_C); signcalc8(reg_B);
         break;
       case 0x3C:  //bit_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {instr_bit8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_bit16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x3D:  //and_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x3E:  //rol_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, instr_rol8(regs, read_byte_65816(effaddr_bank, effaddr_word)));} else {write_word_65816(effaddr_bank, effaddr_word, instr_rol16(regs, read_word_65816(effaddr_bank, effaddr_word)));}
         break;
       case 0x3F:  //and_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {reg_A = reg_A & read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C & read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x40:  //rti_imp
         reg_status = pull_byte(regs); reg_PC = pull_word(regs); reg_PB = pull_byte(regs);
         break;
       case 0x41:  //eor_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x42:  //wdm_imm8
         instr_wdm_65816(regs, read_byte_65816(reg_PB, reg_PC++));
         break;
       case 0x43:  //eor_ds
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x44:  //mvp_abso
         instr_mvp(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)), read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)+1));
         reg_PC+=2;
         break;
       case 0x45:  //eor_zp
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x46:  //lsr_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_lsr8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_lsr16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x47:  //eor_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x48:  //pha_imp
         if(reg_status & FLAG_MS) {push_byte(regs, reg_A);} else {push_word(regs, reg_C);}
         break;
       case 0x49:  //eor_imma
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(reg_PB, reg_PC); zerocalc16(reg_C); signcalc8(reg_B); reg_PC+=2;}
         break;
       case 0x4A:  //lsr_acc
         if(reg_status & FLAG_MS) {reg_A = instr_lsr8(regs, reg_A);} else {reg_C = instr_lsr16(regs, reg_C);}
         break;
       case 0x4B:  //phk_imp
         push_byte(regs, reg_PB);
         break;
       case 0x4C:  //jmp_abso
         reg_PC = read_word_65816(reg_PB, reg_PC);
         break;
       case 0x4D:  //eor_abso
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc16(reg_C); signcalc8(reg_B);}
         reg_PC+=2;
         break;
       case 0x4E:  //lsr_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_lsr8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_lsr16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0x4F:  //eor_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x50:  //bvc_rel
         if ((reg_status & FLAG_OVERFLOW) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x51:  //eor_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x52:  //eor_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x53:  //eor_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x54:  //mvn_abso
         instr_mvn(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)), read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)+1));
         reg_PC+=2;
         break;
       case 0x55:  //eor_zpx
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x56:  //lsr_zpx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_lsr8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_lsr16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x57:  //eor_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x58:  //cli_imp
         clearinterrupt();
         break;
       case 0x59:  //eor_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x5A:  //phy_imp
         if(reg_status & FLAG_XS) {push_byte(regs, reg_Y_low);} else {push_word(regs, reg_Y);}
         break;
       case 0x5B:  //tcd_imp
         reg_DR = reg_C; zerocalc16(reg_DR); signcalc8(reg_DR_high);
         break;
       case 0x5C:  //jml_absl
         effaddr_bank = read_byte_65816(reg_PB, reg_PC+2); reg_PC = read_word_65816(reg_PB, reg_PC); reg_PB = effaddr_bank;
         break;
       case 0x5D:  //eor_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x5E:  //lsr_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, instr_lsr8(regs, read_byte_65816(effaddr_bank, effaddr_word)));} else {write_word_65816(effaddr_bank, effaddr_word, instr_lsr16(regs, read_word_65816(effaddr_bank, effaddr_word)));}
         break;
       case 0x5F:  //eor_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {reg_A = reg_A ^ read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = reg_C ^ read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x60:  //rts_imp
         reg_PC = (pull_word(regs)+1);
         break;
       case 0x61:  //adc_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(reg_DB, effaddr_word));}
         break;
       case 0x62:  //per_rell
         effaddr_word = read_word_65816(reg_PB, reg_PC); reg_PC+=2; push_word(regs, reg_PC+effaddr_word);
         break;
       case 0x63:  //adc_ds
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));} else {reg_C = instr_adc16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));}
         break;
       case 0x64:  //stz_zp
         if(reg_status & FLAG_MS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, 0);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, 0);}
         break;
       case 0x65:  //adc_zp
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));} else {reg_C = instr_adc16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));}
         break;
       case 0x66:  //ror_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_ror8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_ror16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x67:  //adc_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x68:  //pla_imp
         if(reg_status & FLAG_MS) {reg_A = pull_byte(regs); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = pull_word(regs); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x69:  //adc_imma
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(reg_PB, reg_PC++));} else {reg_C = instr_adc16(regs, read_word_65816(reg_PB, reg_PC)); reg_PC+=2;}
         break;
       case 0x6A:  //ror_acc
         if(reg_status & FLAG_MS) {reg_A = instr_ror8(regs, reg_A);} else {reg_C = instr_ror16(regs, reg_C);}
         break;
       case 0x6B:  //rtl_imp
         reg_PC = (pull_word(regs)+1); reg_PB = pull_byte(regs);
         break;
       case 0x6C:  //jmp_abind
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         reg_PC = read_word_65816(reg_PB, effaddr_word);
         break;
       case 0x6D:  //adc_abso
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));} else {reg_C = instr_adc16(regs, read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));}
         reg_PC+=2;
         break;
       case 0x6E:  //ror_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_ror8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_ror16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0x6F:  //adc_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x70:  //bvs_rel
         if ((reg_status & FLAG_OVERFLOW) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x71:  //adc_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x72:  //adc_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(reg_DB, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(reg_DB, effaddr_word));}
         break;
       case 0x73:  //adc_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x74:  //stz_zpx
         if(reg_status & FLAG_MS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X, 0);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X, 0);}
         break;
       case 0x75:  //adc_zpx
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));} else {reg_C = instr_adc16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));}
         break;
       case 0x76:  //ror_zpx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_ror8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_ror16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0x77:  //adc_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x78:  //sei_imp
         setinterrupt();
         break;
       case 0x79:  //adc_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x7A:  //ply_imp
         if(reg_status & FLAG_XS) {reg_Y = pull_byte(regs); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = pull_word(regs); zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0x7B:  //tdc_imp
         reg_C = reg_DR; zerocalc16(reg_C); signcalc8(reg_B);
         break;
       case 0x7C:  //jmp_abindx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         reg_PC = read_word_65816(reg_PB, effaddr_word + reg_X);
         break;
       case 0x7D:  //adc_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x7E:  //ror_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, instr_ror8(regs, read_byte_65816(effaddr_bank, effaddr_word)));} else {write_word_65816(effaddr_bank, effaddr_word, instr_ror16(regs, read_word_65816(effaddr_bank, effaddr_word)));}
         break;
       case 0x7F:  //adc_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {reg_A = instr_adc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_adc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0x80:  //bra_rel
         reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1;
         break;
       case 0x81:  //sta_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, reg_A);} else {write_word_65816(reg_DB, effaddr_word, reg_C);}
         break;
       case 0x82:  //brl_rell
         reg_PC += read_word_65816(reg_PB, reg_PC) + 1;
         break;
       case 0x83:  //sta_ds
         if(reg_status & FLAG_MS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP, reg_A);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP, reg_C);}
         break;
       case 0x84:  //sty_zp
         if(reg_status & FLAG_XS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, reg_Y_low);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, reg_Y);}
         break;
       case 0x85:  //sta_zp
         if(reg_status & FLAG_MS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, reg_A);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, reg_C);}
         break;
       case 0x86:  //stx_zp
         if(reg_status & FLAG_XS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, reg_X_low);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR, reg_X);}
         break;
       case 0x87:  //sta_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0x88:  //dey_imp
         if(reg_status & FLAG_XS) {reg_Y_low--; zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y--; zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0x89:  //bit_imma
         if(reg_status & FLAG_MS) {instr_bit8(regs, read_byte_65816(reg_PB, reg_PC++));} else {instr_bit16(regs, read_word_65816(reg_PB, reg_PC)); reg_PC+=2;}
         break;
       case 0x8A:  //txa_imp
         if(reg_status & FLAG_MS) {reg_A = reg_X_low; zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = ((reg_status & FLAG_XS) ? reg_X_low : reg_X); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x8B:  //phb_imp
         push_byte(regs, reg_DB);
         break;
       case 0x8C:  //sty_abso
         if(reg_status & FLAG_XS) {write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_Y_low);} else {write_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_Y);}
         reg_PC+=2;
         break;
       case 0x8D:  //sta_abso
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_A);} else {write_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_C);}
         reg_PC+=2;
         break;
       case 0x8E:  //stx_abso
         if(reg_status & FLAG_XS) {write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_X_low);} else {write_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC), reg_X);}
         reg_PC+=2;
         break;
       case 0x8F:  //sta_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0x90:  //bcc_rel
         if ((reg_status & FLAG_CARRY) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0x91:  //sta_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0x92:  //sta_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, reg_A);} else {write_word_65816(reg_DB, effaddr_word, reg_C);}
         break;
       case 0x93:  //sta_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0x94:  //sty_zpx
         if(reg_status & FLAG_XS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X, reg_Y_low);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X, reg_Y);}
         break;
       case 0x95:  //sta_zpx
         if(reg_status & FLAG_MS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X, reg_A);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X, reg_C);}
         break;
       case 0x96:  //stx_zpy
         if(reg_status & FLAG_XS) {write_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_Y, reg_X_low);} else {write_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_Y, reg_X);}
         break;
       case 0x97:  //sta_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0x98:  //tya_imp
         if(reg_status & FLAG_MS) {reg_A = reg_Y_low; zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = ((reg_status & FLAG_XS) ? reg_Y_low : reg_Y); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0x99:  //sta_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0x9A:  //txs_imp
         if(reg_status & FLAG_XS) { reg_SP = reg_X_low;} else {reg_SP = reg_X;}
         break;
       case 0x9B:  //txy_imp
         if(reg_status & FLAG_XS) {reg_Y = reg_X_low; zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = reg_X; zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0x9C:  //stz_abso
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC), 0);} else {write_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC), 0);}
         reg_PC+=2;
         break;
       case 0x9D:  //sta_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0x9E:  //stz_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, 0);} else {write_word_65816(effaddr_bank, effaddr_word, 0);}
         break;
       case 0x9F:  //sta_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, reg_A);} else {write_word_65816(effaddr_bank, effaddr_word, reg_C);}
         break;
       case 0xA0:  //ldy_immx
         if(reg_status & FLAG_XS) {reg_Y = read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = read_word_65816(reg_PB, reg_PC); zerocalc16(reg_Y); signcalc16(reg_Y); reg_PC+=2;}
         break;
       case 0xA1:  //lda_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xA2:  //ldx_immx
         if(reg_status & FLAG_XS) {reg_X = read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = read_word_65816(reg_PB, reg_PC); zerocalc16(reg_X); signcalc16(reg_X); reg_PC+=2;}
         break;
       case 0xA3:  //lda_ds
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xA4:  //ldy_zp
         if(reg_status & FLAG_XS) {reg_Y = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0xA5:  //lda_zp
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xA6:  //ldx_zp
         if(reg_status & FLAG_XS) {reg_X = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR); zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xA7:  //lda_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xA8:  //tay_imp
         if(reg_status & FLAG_XS) {reg_Y = reg_A; zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = reg_C; zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0xA9:  //lda_imma
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(reg_PB, reg_PC++); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(reg_PB, reg_PC); zerocalc16(reg_C); signcalc8(reg_B); reg_PC+=2;}
         break;
       case 0xAA:  //tax_imp
         if(reg_status & FLAG_XS) {reg_X = reg_A; zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = reg_C; zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xAB:  //plb_imp
         reg_DB = pull_byte(regs); zerocalc8(reg_DB); signcalc8(reg_DB);
         break;
       case 0xAC:  //ldy_abso
         if(reg_status & FLAG_XS) {reg_Y = read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc16(reg_Y); signcalc16(reg_Y);}
         reg_PC+=2;
         break;
       case 0xAD:  //lda_abso
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc16(reg_C); signcalc8(reg_B);}
         reg_PC+=2;
         break;
       case 0xAE:  //ldx_abso
         if(reg_status & FLAG_XS) {reg_X = read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)); zerocalc16(reg_X); signcalc16(reg_X);}
         reg_PC+=2;
         break;
       case 0xAF:  //lda_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xB0:  //bcs_rel
         if ((reg_status & FLAG_CARRY) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0xB1:  //lda_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xB2:  //lda_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(reg_DB, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(reg_DB, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xB3:  //lda_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xB4:  //ldy_zpx
         if(reg_status & FLAG_XS) {reg_Y = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0xB5:  //lda_zpx
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xB6:  //ldx_zpy
         if(reg_status & FLAG_XS) {reg_X = read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_Y); zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_Y); zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xB7:  //lda_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xB8:  //clv_imp
         clearoverflow();
         break;
       case 0xB9:  //lda_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xBA:  //tsx_imp
         if(reg_status & FLAG_XS) { reg_X = reg_SP_low; zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = reg_SP; zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xBB:  //tyx_imp
         if(reg_status & FLAG_XS) {reg_X = reg_Y_low; zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = reg_Y; zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xBC:  //ldy_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_XS) {reg_Y = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0xBD:  //lda_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xBE:  //ldx_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_XS) {reg_X = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xBF:  //lda_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {reg_A = read_byte_65816(effaddr_bank, effaddr_word); zerocalc8(reg_A); signcalc8(reg_A);} else {reg_C = read_word_65816(effaddr_bank, effaddr_word); zerocalc16(reg_C); signcalc8(reg_B);}
         break;
       case 0xC0:  //cpy_immx
         if(reg_status & FLAG_XS) {instr_cpy8(regs, read_byte_65816(reg_PB, reg_PC++));} else {instr_cpy16(regs, read_word_65816(reg_PB, reg_PC)); reg_PC+=2;}
         break;
       case 0xC1:  //cmp_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));} else {instr_cmp16(regs, read_word_65816(reg_DB, effaddr_word));}
         break;
       case 0xC2:  //rep_imm8
         reg_status &= (~(read_byte_65816(reg_PB, reg_PC++)));
         break;
       case 0xC3:  //cmp_ds
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));} else {instr_cmp16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));}
         break;
       case 0xC4:  //cpy_zp
         if(reg_status & FLAG_XS) {instr_cpy8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));} else {instr_cpy16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));}
         break;
       case 0xC5:  //cmp_zp
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));} else {instr_cmp16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));}
         break;
       case 0xC6:  //dec_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_dec8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_dec16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0xC7:  //cmp_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xC8:  //iny_imp
         if(reg_status & FLAG_XS) {reg_Y_low++; zerocalc8(reg_Y_low); signcalc8(reg_Y_low);} else {reg_Y++; zerocalc16(reg_Y); signcalc16(reg_Y);}
         break;
       case 0xC9:  //cmp_imma
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(reg_PB, reg_PC++));} else {instr_cmp16(regs, read_word_65816(reg_PB, reg_PC)); reg_PC+=2;}
         break;
       case 0xCA:  //dex_imp
         if(reg_status & FLAG_XS) {reg_X_low--; zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X--; zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xCB:  //wai_imp
         instr_wai_65816(regs);
         break;
       case 0xCC:  //cpy_abso
         if(reg_status & FLAG_XS) {instr_cpy8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));} else {instr_cpy16(regs, read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));}
         reg_PC+=2;
         break;
       case 0xCD:  //cmp_abso
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));} else {instr_cmp16(regs, read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));}
         reg_PC+=2;
         break;
       case 0xCE:  //dec_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_dec8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_dec16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0xCF:  //cmp_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xD0:  //bne_rel
         if ((reg_status & FLAG_ZERO) == 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0xD1:  //cmp_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xD2:  //cmp_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(reg_DB, effaddr_word));} else {instr_cmp16(regs, read_word_65816(reg_DB, effaddr_word));}
         break;
       case 0xD3:  //cmp_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xD4:  //pei_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         push_word(regs, effaddr_word);
         break;
       case 0xD5:  //cmp_zpx
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));} else {instr_cmp16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));}
         break;
       case 0xD6:  //dec_zpx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_dec8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_dec16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0xD7:  //cmp_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xD8:  //cld_imp
         cleardecimal();
         break;
       case 0xD9:  //cmp_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xDA:  //phx_imp
         if(reg_status & FLAG_XS) {push_byte(regs, reg_X_low);} else {push_word(regs, reg_X);}
         break;
       case 0xDB:  //stp_imp
         instr_stp_65816(regs);
         break;
       case 0xDC:  //jml_abindl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         reg_PB = effaddr_bank; reg_PC = effaddr_word;
         break;
       case 0xDD:  //cmp_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xDE:  //dec_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, instr_dec8(regs, read_byte_65816(effaddr_bank, effaddr_word)));} else {write_word_65816(effaddr_bank, effaddr_word, instr_dec16(regs, read_word_65816(effaddr_bank, effaddr_word)));}
         break;
       case 0xDF:  //cmp_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {instr_cmp8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {instr_cmp16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xE0:  //cpx_immx
         if(reg_status & FLAG_XS) {instr_cpx8(regs, read_byte_65816(reg_PB, reg_PC++));} else {instr_cpx16(regs, read_word_65816(reg_PB, reg_PC)); reg_PC+=2;}
         break;
       case 0xE1:  //sbc_indx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(reg_DB, effaddr_word));}
         break;
       case 0xE2:  //sep_imm8
         reg_status |= read_byte_65816(reg_PB, reg_PC++); if(reg_status & FLAG_XS){ reg_X_high = 0; reg_Y_high = 0;}
         break;
       case 0xE3:  //sbc_ds
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));} else {reg_C = instr_sbc16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_SP));}
         break;
       case 0xE4:  //cpx_zp
         if(reg_status & FLAG_XS) {instr_cpx8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));} else {instr_cpx16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));}
         break;
       case 0xE5:  //sbc_zp
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));} else {reg_C = instr_sbc16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR));}
         break;
       case 0xE6:  //inc_zp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_inc8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_inc16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0xE7:  //sbc_indl
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xE8:  //inx_imp
         if(reg_status & FLAG_XS) {reg_X_low++; zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X++; zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xE9:  //sbc_imma
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(reg_PB, reg_PC++));} else {reg_C = instr_sbc16(regs, read_word_65816(reg_PB, reg_PC)); reg_PC+=2;}
         break;
       case 0xEA:  //nop_imp
         //*** Unimplemented ***
         break;
       case 0xEB:  //xba_imp
         effaddr_low = reg_A; reg_A = reg_B; reg_B = effaddr_low; zerocalc8(reg_A); signcalc8(reg_A);
         break;
       case 0xEC:  //cpx_abso
         if(reg_status & FLAG_XS) {instr_cpx8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));} else {instr_cpx16(regs, read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));}
         reg_PC+=2;
         break;
       case 0xED:  //sbc_abso
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));} else {reg_C = instr_sbc16(regs, read_word_65816(reg_DB, read_word_65816(reg_PB, reg_PC)));}
         reg_PC+=2;
         break;
       case 0xEE:  //inc_abso
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         if(reg_status & FLAG_MS) {write_byte_65816(reg_DB, effaddr_word, instr_inc8(regs, read_byte_65816(reg_DB, effaddr_word)));} else {write_word_65816(reg_DB, effaddr_word, instr_inc16(regs, read_word_65816(reg_DB, effaddr_word)));}
         reg_PC+=2;
         break;
       case 0xEF:  //sbc_absl
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xF0:  //beq_rel
         if ((reg_status & FLAG_ZERO) != 0) { reg_PC += sign_extend(read_byte_65816(reg_PB, reg_PC)) + 1; } else { reg_PC++; }
         break;
       case 0xF1:  //sbc_indy
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xF2:  //sbc_inzp
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_word = read_word_65816(0, effaddr_word);
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(reg_DB, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(reg_DB, effaddr_word));}
         break;
       case 0xF3:  //sbc_dsy
         effaddr_word = read_byte_65816(reg_PB, reg_PC++) + reg_SP;
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xF4:  //pea_abso
         push_word(regs, read_word_65816(reg_PB, reg_PC)); 
         reg_PC+=2;
         break;
       case 0xF5:  //sbc_zpx
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));} else {reg_C = instr_sbc16(regs, read_word_65816(0, (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X));}
         break;
       case 0xF6:  //inc_zpx
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR+reg_X;
         if(reg_status & FLAG_MS) {write_byte_65816(0, effaddr_word, instr_inc8(regs, read_byte_65816(0, effaddr_word)));} else {write_word_65816(0, effaddr_word, instr_inc16(regs, read_word_65816(0, effaddr_word)));}
         break;
       case 0xF7:  //sbc_indly
         effaddr_word = (uint16_t) read_byte_65816(reg_PB, reg_PC++)+reg_DR;
         effaddr_bank = read_byte_65816(0, effaddr_word+2);
         effaddr_word = read_word_65816(0, effaddr_word);
         effaddr_full += (uint32_t) reg_Y;
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xF8:  //sed_imp
         setdecimal();
         break;
       case 0xF9:  //sbc_absy
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_Y;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xFA:  //plx_imp
         if(reg_status & FLAG_XS) {reg_X = pull_byte(regs); zerocalc8(reg_X_low); signcalc8(reg_X_low);} else {reg_X = pull_word(regs); zerocalc16(reg_X); signcalc16(reg_X);}
         break;
       case 0xFB:  //xce_imp
         instr_xce(regs);
         return;
       case 0xFC:  //jsr_abindx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         push_word(regs ,reg_PC+1); reg_PC = read_word_65816(reg_PB, reg_PC);
         break;
       case 0xFD:  //sbc_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       case 0xFE:  //inc_absx
         effaddr_bank = reg_DB; effaddr_word = read_word_65816(reg_PB, reg_PC); effaddr_full+=(uint32_t)reg_X;
         reg_PC+=2;
         if(reg_status & FLAG_MS) {write_byte_65816(effaddr_bank, effaddr_word, instr_inc8(regs, read_byte_65816(effaddr_bank, effaddr_word)));} else {write_word_65816(effaddr_bank, effaddr_word, instr_inc16(regs, read_word_65816(effaddr_bank, effaddr_word)));}
         break;
       case 0xFF:  //sbc_abslx
         effaddr_word = read_word_65816(reg_PB, reg_PC);
         reg_PC+=2;
         effaddr_bank = read_byte_65816(reg_PB, reg_PC++);
         effaddr_full += (uint32_t) reg_X_low;
         if(reg_status & FLAG_MS) {reg_A = instr_sbc8(regs, read_byte_65816(effaddr_bank, effaddr_word));} else {reg_C = instr_sbc16(regs, read_word_65816(effaddr_bank, effaddr_word));}
         break;
       default:
         return;
    }
}


