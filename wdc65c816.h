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



typedef struct 
{
   //shuint32 effective_address;
   shuint16 PC;
   shuint16 C;
   shuint16 X;
   shuint16 Y;
   shuint16 S;
   shuint16 DR;
   uint8_t status;
   uint8_t e_flag;
   uint8_t DB, PB;
   uint8_t int_status;
} WDC65C816_Regs;


//65816 defines
#define FLAG_CARRY     0x01
#define FLAG_EMU       0x01
#define FLAG_ZERO      0x02
#define FLAG_INTERRUPT 0x04
#define FLAG_NMI       0x08
#define FLAG_DECIMAL   0x08
#define FLAG_BREAK     0x10
#define FLAG_MS        0x20
#define FLAG_XS        0x10
#define FLAG_CONSTANT  0x20
#define FLAG_OVERFLOW  0x40
#define FLAG_SIGN      0x80

#define BASE_STACK     0x100

#define WDC6502_NMI_VECTOR  0xFFFA
#define WDC6502_RESET_VECTOR  0xFFFC
#define WDC6502_IRQ_VECTOR  0xFFFE
#define WDC6502_ABORT_VECTOR  0xFFF8
#define WDC6502_COP_VECTOR  0xFFF4

#define WDC65816_NMI_VECTOR  0xFFEA
#define WDC65816_BRK_VECTOR  0xFFE6
#define WDC65816_IRQ_VECTOR  0xFFEE
#define WDC65816_ABORT_VECTOR  0xFFE8
#define WDC65816_COP_VECTOR  0xFFE4



//flag modifier macros
#define setcarry() reg_status |= FLAG_CARRY
#define clearcarry() reg_status &= (~FLAG_CARRY)
#define setzero() reg_status |= FLAG_ZERO
#define clearzero() reg_status &= (~FLAG_ZERO)
#define setinterrupt() reg_status |= FLAG_INTERRUPT
#define clearinterrupt() reg_status &= (~FLAG_INTERRUPT)
#define setdecimal() reg_status |= FLAG_DECIMAL
#define cleardecimal() reg_status &= (~FLAG_DECIMAL)
#define setoverflow() reg_status |= FLAG_OVERFLOW
#define clearoverflow() reg_status &= (~FLAG_OVERFLOW)
#define setsign() reg_status |= FLAG_SIGN
#define clearsign() reg_status &= (~FLAG_SIGN)


//flag calculation macros
#define zerocalc8(n) {\
    if ((n) & 0xFF) clearzero();\
        else setzero();\
}

#define signcalc8(n) {\
    if ((n) & 0x80) setsign();\
        else clearsign();\
}

/*
#define carrycalc8(n) {\
    if ((n.byte[NATIVE_BYTE_HIGH]) & 0xFF) setcarry();\
        else clearcarry();\
}
*/

#define overflowcalc8(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n.byte[NATIVE_BYTE_LOW]) ^ (m)) & ((n.byte[NATIVE_BYTE_LOW]) ^ (o)) & 0x80) setoverflow();\
        else clearoverflow();\
}

///////////////////////////////////
#define zerocalc16(n) {\
    if ((n) & 0xFFFF) clearzero();\
        else setzero();\
}

#define signcalc16(n) {\
    if ((n) & 0x8000) setsign();\
        else clearsign();\
}

/*
#define carrycalc16(n) {\
    if ((n.byte[NATIVE_BYTE_UPPER]) & 0xFF) setcarry();\
        else clearcarry();\
}
*/

#define overflowcalc16(n, m, o) { /* n = result, m = accumulator, o = memory */ \
    if (((n.word[NATIVE_WORD_LOW]) ^ (m)) & ((n.word[NATIVE_WORD_LOW]) ^ (o)) & 0x8000) setoverflow();\
        else clearoverflow();\
}

#define update_flag(c,f) {\
    if (c) reg_status |= f;\
        else reg_status &= ~f;\
}



#define effaddr_bank   effective_address.byte[NATIVE_BYTE_UPPER]
#define effaddr_high   effective_address.byte[NATIVE_BYTE_HIGH]
#define effaddr_low    effective_address.byte[NATIVE_BYTE_LOW]
#define effaddr_word   effective_address.word[NATIVE_WORD_LOW]
#define effaddr_full   effective_address.dword
#define reg_A  regs->C.byte[NATIVE_BYTE_LOW]
#define reg_B  regs->C.byte[NATIVE_BYTE_HIGH]
#define reg_C  regs->C.word
#define reg_X  regs->X.word
#define reg_Y  regs->Y.word
#define reg_X_low  regs->X.byte[NATIVE_BYTE_LOW]
#define reg_Y_low  regs->Y.byte[NATIVE_BYTE_LOW]
#define reg_X_high  regs->X.byte[NATIVE_BYTE_HIGH]
#define reg_Y_high  regs->Y.byte[NATIVE_BYTE_HIGH]
#define reg_DR   regs->DR.word
#define reg_DR_high  regs->DR.byte[NATIVE_BYTE_HIGH]
#define reg_PC  regs->PC.word
#define reg_PC_low  regs->PC.byte[NATIVE_BYTE_LOW]
#define reg_PC_high  regs->PC.byte[NATIVE_BYTE_HIGH]
#define reg_SP  regs->S.word
#define reg_SP_low  regs->S.byte[NATIVE_BYTE_LOW]
#define reg_SP_high  regs->S.byte[NATIVE_BYTE_HIGH]
#define reg_PB  regs->PB
#define reg_DB  regs->DB
#define reg_e_flag   regs->e_flag
#define reg_status   regs->status
#define interrupt_status  regs->int_status

//#define saveaccum8(n) reg_A = (uint8_t)(n.byte[NATIVE_BYTE_LOW])
//#define saveaccum16(n) reg_C = (uint16_t)(n.word[NATIVE_WORD_LOW])


void reset_65816(WDC65C816_Regs *regs);
void nmi_65816(WDC65C816_Regs *regs);
void irq_65816(WDC65C816_Regs *regs);
void step_65816(WDC65C816_Regs *regs);


