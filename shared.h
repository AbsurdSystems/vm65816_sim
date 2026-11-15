
//#define NATIVE_BIG_ENDIAN

/*
    #ifdef NATIVE_BIG_ENDIAN
    opcode = read6502(reg_pc.byte[0],reg_pc.byte[1]);
    #else 
    opcode = read6502(reg_pc.byte[1],reg_pc.byte[0]);
    #endif    
*/

#ifndef NATIVE_BIG_ENDIAN

#define NATIVE_BYTE_LOW 0
#define NATIVE_BYTE_HIGH 1
#define NATIVE_BYTE_UPPER 2
#define NATIVE_BYTE_TOP 3
#define NATIVE_WORD_LOW 0
#define NATIVE_WORD_HIGH 1

#endif

/*
#define dword_low_byte(var)    ((shuint32) var).byte[NATIVE_BYTE_LOW]
#define dword_high_byte(var)    ((shuint32) var).byte[NATIVE_BYTE_HIGH]
#define dword_upper_byte(var)    ((shuint32) var).byte[NATIVE_BYTE_UPPER]
#define dword_top_byte(var)    ((shuint32) var).byte[NATIVE_BYTE_TOP]

#define word_low_byte(var)    ((shuint16) var).byte[NATIVE_BYTE_LOW]
#define word_high_byte(var)    ((shuint16) var).byte[NATIVE_BYTE_HIGH]
*/

typedef union Shared_Uint32
{
   uint32_t dword;
   uint16_t word[2];
   uint8_t byte[4];
} shuint32;

typedef union Shared_Uint16
{
   uint16_t word;
   uint8_t byte[2];
} shuint16;

