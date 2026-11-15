import string
import re


#inlzp = [zp]
#laddr
#dW = d,W
#indWy = (d,W),Y

#		0      1        2	   3      4       5       6        7        8	    9       A       B		C         D       E       F
main_addrtable = [
      'imp', 'indx', 'imm8', 'ds',   'zp',   'zp',   'zp',  'indl',   'imp', 'imma',  'acc',  'imp', 'abso',   'abso', 'abso',  'absl',  #0
      'rel', 'indy', 'inzp', 'dsy',  'zp',   'zpx',  'zpx', 'indly',  'imp', 'absy',  'acc',  'imp', 'abso',   'absx', 'absx',  'abslx', #1
      'abso','indx', 'absl', 'ds',   'zp',   'zp',   'zp',  'indl',   'imp', 'imma',  'acc',  'imp', 'abso',   'abso', 'abso',  'absl',  #2
      'rel', 'indy', 'inzp', 'dsy',  'zpx',  'zpx',  'zpx', 'indly',  'imp', 'absy',  'acc',  'imp', 'absx',   'absx', 'absx',  'abslx', #3
      'imp', 'indx', 'imm8', 'ds',   'abso', 'zp',   'zp',  'indl',   'imp', 'imma',  'acc',  'imp', 'abso',   'abso', 'abso',  'absl',  #4
      'rel', 'indy', 'inzp', 'dsy',  'abso', 'zpx',  'zpx', 'indly',  'imp', 'absy',  'imp',  'imp', 'absl',   'absx', 'absx',  'abslx', #5
      'imp', 'indx', 'rell', 'ds',   'zp',   'zp',   'zp',  'indl',   'imp', 'imma',  'acc',  'imp', 'abind',  'abso', 'abso',  'absl',  #6
      'rel', 'indy', 'inzp', 'dsy',  'zpx',  'zpx',  'zpx', 'indly',  'imp', 'absy',  'imp',  'imp', 'abindx', 'absx', 'absx',  'abslx', #7
      'rel', 'indx', 'rell', 'ds',   'zp',   'zp',   'zp',  'indl',   'imp', 'imma',  'imp',  'imp', 'abso',   'abso', 'abso',  'absl',  #8
      'rel', 'indy', 'inzp', 'dsy',  'zpx',  'zpx',  'zpy', 'indly',  'imp', 'absy',  'imp',  'imp', 'abso',   'absx', 'absx',  'abslx', #9
      'immx','indx', 'immx', 'ds',   'zp',   'zp',   'zp',  'indl',   'imp', 'imma',  'imp',  'imp', 'abs',    'abso', 'abso',  'absl',  #A
      'rel', 'indy', 'inzp', 'dsy',  'zpx',  'zpx',  'zpy', 'indly',  'imp', 'absy',  'imp',  'imp', 'absx',   'absx', 'absy',  'abslx', #B
      'immx','indx', 'imm8', 'ds',   'zp',   'zp',   'zp',  'indl',   'imp', 'imma',  'imp',  'imp', 'abso',   'abso', 'abso',  'absl',  #C
      'rel', 'indy', 'inzp', 'dsy',  'inzp', 'zpx',  'zpx', 'indly',  'imp', 'absy',  'imp',  'imp', 'abindl', 'absx', 'absx',  'abslx', #D
      'immx','indx', 'imm8', 'ds',   'zp',   'zp',   'zp',  'indl',   'imp', 'imma',  'imp',  'imp', 'abso',   'abso', 'abso',  'absl',  #E
      'rel', 'indy', 'inzp', 'dsy',  'abso', 'zpx',  'zpx', 'indly',  'imp', 'absy',  'imp',  'imp', 'abindx','absx', 'absx',  'abslx'  #F
]

# 0       1       2       3       4       5       6       7       8       9      10      11      12      13      14      15
main_optable = [
'brk',  'ora',  'cop',  'ora',  'tsb',  'ora',  'asl',  'ora',  'php',  'ora',  'asl',  'phd',  'tsb',  'ora',  'asl',  'ora', #0 
'bpl',  'ora',  'ora',  'ora',  'trb',  'ora',  'asl',  'ora',  'clc',  'ora',  'inc',  'tcs',  'trb',  'ora',  'asl',  'ora', #1
'jsr',  'and',  'jsl',  'and',  'bit',  'and',  'rol',  'and',  'plp',  'and',  'rol',  'pld',  'bit',  'and',  'rol',  'and', #2
'bmi',  'and',  'and',  'and',  'bit',  'and',  'rol',  'and',  'sec',  'and',  'dec',  'tsc',  'bit',  'and',  'rol',  'and', #3
'rti',  'eor',  'wdm',  'eor',  'mvp',  'eor',  'lsr',  'eor',  'pha',  'eor',  'lsr',  'phk',  'jmp',  'eor',  'lsr',  'eor', #4
'bvc',  'eor',  'eor',  'eor',  'mvn',  'eor',  'lsr',  'eor',  'cli',  'eor',  'phy',  'tcd',  'jmp',  'eor',  'lsr',  'eor', #5
'rts',  'adc',  'per',  'adc',  'stz',  'adc',  'ror',  'adc',  'pla',  'adc',  'ror',  'rtl',  'jmp',  'adc',  'ror',  'adc', #6
'bvs',  'adc',  'adc',  'adc',  'stz',  'adc',  'ror',  'adc',  'sei',  'adc',  'ply',  'tdc',  'jmp',  'adc',  'ror',  'adc', #7
'bra',  'sta',  'brl',  'sta',  'sty',  'sta',  'stx',  'sta',  'dey',  'bit',  'txa',  'phb',  'sty',  'sta',  'stx',  'sta', #8
'bcc',  'sta',  'sta',  'sta',  'sty',  'sta',  'stx',  'sta',  'tya',  'sta',  'txs',  'txy',  'stz',  'sta',  'stz',  'sta', #9
'ldy',  'lda',  'ldx',  'lda',  'ldy',  'lda',  'ldx',  'lda',  'tay',  'lda',  'tax',  'plb',  'ldy',  'lda',  'ldx',  'lda', #A
'bcs',  'lda',  'lda',  'lda',  'ldy',  'lda',  'ldx',  'lda',  'clv',  'lda',  'tsx',  'tyx',  'ldy',  'lda',  'ldx',  'lda', #B
'cpy',  'cmp',  'rep',  'cmp',  'cpy',  'cmp',  'dec',  'cmp',  'iny',  'cmp',  'dex',  'wai',  'cpy',  'cmp',  'dec',  'cmp', #C
'bne',  'cmp',  'cmp',  'cmp',  'pei',  'cmp',  'dec',  'cmp',  'cld',  'cmp',  'phx',  'stp',  'jml',  'cmp',  'dec',  'cmp', #D
'cpx',  'sbc',  'sep',  'sbc',  'cpx',  'sbc',  'inc',  'sbc',  'inx',  'sbc',  'nop',  'xba',  'cpx',  'sbc',  'inc',  'sbc', #E
'beq',  'sbc',  'sbc',  'sbc',  'pea',  'sbc',  'inc',  'sbc',  'sed',  'sbc',  'plx',  'xce',  'jsr',  'sbc',  'inc',  'sbc'  #F
]




modify_memory_instrs = {'tsb':[],'trb':[],'rol':[],'ror':[],'lsr':[],'asl':[],'dec':[],'inc':[],'smb':[],'rmb':[]}

dualmode_acc_instrs = {'ora','and','adc','sbc','lda','sta','inc','dec','cmp','eor'}
dualmode_index_instrs = {'ldx','stx','ldy','sty','cpx','cpy'}


native_instr_code_dict = {'absa': "",
                         'pha': "push_byte(reg_A);",
                         'phc': "push_word(reg_C.word);",
                         'phw': "push_word(reg_W);",
                         'phx': "push_byte(reg_X);",
                         'bra': "reg_PC += effective_address;",
                         'bcc': "if ((reg_P & FLAG_CARRY) == 0) { reg_PC += effective_address; }",
                         'bcs': "if ((reg_P & FLAG_CARRY) != 0) { reg_PC += effective_address; }",
                         'beq': "if ((reg_P & FLAG_ZERO) != 0) { reg_PC += effective_address; }",
                         'bne': "if ((reg_P & FLAG_ZERO) == 0) { reg_PC += effective_address; }",
                         'bmi': "if ((reg_P & FLAG_SIGN) != 0) { reg_PC += effective_address; }",
                         'bpl': "if ((reg_P & FLAG_SIGN) == 0) { reg_PC += effective_address; }",
                         'bvc': "if ((reg_P & FLAG_OVERFLOW) == 0) { reg_PC += effective_address; }",
                         'bvs': "if ((reg_P & FLAG_OVERFLOW) != 0) { reg_PC += effective_address; }",
                         'jsr': "push_word(reg_PC-1); reg_PC = effective_address; //%s %s",
                         'jmp': "reg_PC = effective_address; //%s %s",
                         'brk': "instr_brk();",
                         'clc': "clearcarry();",
                         'cld': "cleardecimal();",
                         'cli': "clearinterrupt();",
                         'clv': "clearoverflow();",
                         'phy': "push_byte(reg_Y);",
                         'php': "push_byte(reg_P);",
                         'plp': "reg_P = pull_byte();",
                         'plc': "reg_C.word = pull_word(); update_flag((reg_C.word == 0), FLAG_ZERO); update_flag(((reg_C.word & 0x8000) != 0), FLAG_ZERO);",
                         'plw': "reg_W = pull_word();",
                         'plc': "reg_C.word = pull_word();",
                         'pla8': "reg_A = pull_byte(); zerocalc(reg_A); signcalc(reg_A);",
                         'plx8': "reg_X = pull_byte(); zerocalc(reg_X); signcalc(reg_X);",
                         'ply8': "reg_Y = pull_byte(); zerocalc(reg_Y); signcalc(reg_Y);",
                         'tsw': "reg_W = reg_S.word;",
                         'tws': "reg_S.word = reg_W;",
                         'xba': "address_byte = reg_A; reg_A = reg_B; reg_B = address_byte;",
                         'tsx': "reg_X = reg_SL; zerocalc(reg_X); signcalc(reg_X);",
                         'tax': "reg_X = reg_A; zerocalc(reg_X); signcalc(reg_X);",
                         'tay': "reg_Y = reg_A; zerocalc(reg_Y); signcalc(reg_Y);",
                         'txa': "reg_A = reg_X; zerocalc(reg_A); signcalc(reg_A);",
                         'tya': "reg_A = reg_Y; zerocalc(reg_A); signcalc(reg_A);",
                         'txs': "reg_SL = reg_X;",
                         'tcz': "reg_Z.word = reg_C.word;",
                         'tzc': "reg_C.word = reg_Z.word;",
                         'tcw': "reg_W = reg_C.word;",
                         'twc': "reg_C.word = reg_W;",
                         'phi': "push_word(read_word_65816(effective_address)); //%s %s",
                         'phzb': "push_byte(read_byte_65816(effective_address));",
                         'phzw': "push_word(read_word_65816(effective_address));",
                         'plzb': "write_byte_65816(effective_address, pull_byte());",
                         'plzw': "write_word_65816(effective_address, pull_word());",
                         'phrr': "instr_phrr(read_byte_65816(effective_address)); //%s %s",
                         'plrr': "instr_plrr(read_byte_65816(effective_address)); //%s %s",
                         'lda': "reg_A = read_byte_%s(%s); zerocalc(reg_A); signcalc(reg_A);",
                         'ldz': "reg_Z.word = read_word_%s(%s);",
                         'ldx': "reg_X = read_byte_%s(%s); zerocalc(reg_X); signcalc(reg_X);",
                         'ldy': "reg_Y = read_byte_%s(%s); zerocalc(reg_Y); signcalc(reg_Y);",
                         'ldb': "reg_B = read_byte_%s(%s);",
                         'sta': "write_byte_%s(%s, reg_A);",
                         'stw': "write_word_%s(%s, reg_W);",
                         'stx': "write_byte_%s(%s, reg_X);",
                         'sty': "write_byte_%s(%s, reg_Y);",
                         'stz': "write_byte_%s(%s, 0);",
                         'sec': "setcarry();",
                         'sed': "setdecimal();",
                         'sei': "setinterrupt();",
                         'rti': "reg_P = pull_byte(); reg_PC = pull_word();",
                         'rts': "reg_PC = (pull_word()+1);",
                         'rtl': "reg_CB = pull_byte(); reg_PC = pull_word();",

                         'adc8': "reg_A = instr_adc(read_byte_%s(%s));",
                         'sbc8': "reg_A = instr_sbc(read_byte_%s(%s));",
                         'bit8': "instr_bit(read_byte_%s(%s));",
                         'and8': "reg_A = reg_A & read_byte_%s(%s); zerocalc(reg_A); signcalc(reg_A);",
                         'ora8': "reg_A = reg_A | read_byte_%s(%s); zerocalc(reg_A); signcalc(reg_A);",
                         'eor8': "reg_A = reg_A ^ read_byte_%s(%s); zerocalc(reg_A); signcalc(reg_A);",
                         'cmp8': "instr_cmp(read_byte_%s(%s));",
                         'cpy8': "instr_cpy(read_byte_%s(%s));",
                         'cpx8': "instr_cpx(read_byte_%s(%s));",
                         'ror8': "%s(%s, instr_ror(%s));",
                         'rol8': "%s(%s, instr_rol(%s));",
                         'asl8': "%s(%s, instr_asl(%s));",
                         'lsr8': "%s(%s, instr_lsr(%s));",
                         'inc8': "%s(%s, instr_inc(%s));",
                         'dec8': "%s(%s, instr_dec(%s));",
                         'tsb8': "%s(%s, instr_tsb(%s));",
                         'trb8': "%s(%s, instr_trb(%s));",


                         'wai': "wait_65816();",
                         'stp': "stop_65816();",
                         'lsrc': "reg_C.word = reg_C.word >> 1; zerocalc16(reg_C.word); signcalc16(reg_C.word);",
                         'aslc': "reg_C.word = reg_C.word << 1; zerocalc16(reg_C.word); signcalc16(reg_C.word);",
                         'incc': "reg_C.word++; zerocalc16(reg_C.word); signcalc16(reg_C.word);",
                         'decc': "reg_C.word--; zerocalc16(reg_C.word); signcalc16(reg_C.word);",
                         'incz': "reg_Z.word++; zerocalc16(reg_Z.word); signcalc16(reg_Z.word);",
                         'decz': "reg_Z.word--; zerocalc16(reg_Z.word); signcalc16(reg_Z.word);",
                         'iny8': "reg_YL++; zerocalc(reg_YL); signcalc(reg_YL);",
                         'dey8': "reg_YL--; zerocalc(reg_YL); signcalc(reg_YL);",
                         'inx8': "reg_XL++; zerocalc(reg_XL); signcalc(reg_XL);",
                         'dex8': "reg_XL--; zerocalc(reg_XL); signcalc(reg_XL);",
                         'iny16': "reg_Y.word++; zerocalc16(reg_Y.word); signcalc16(reg_Y.word);",
                         'dey16': "reg_Y.word--; zerocalc16(reg_Y.word); signcalc16(reg_Y.word);",
                         'inx16': "reg_X.word++; zerocalc16(reg_X.word); signcalc16(reg_X.word);",
                         'dex16': "reg_X.word--; zerocalc16(reg_X.word); signcalc16(reg_X.word);",
                         'cpl': "reg_a = (~(reg_a));"}



def generate_emulator_case_statement(filename,optable,addrtable):
   code_lines = {}
   with open(filename,'w') as f:
      for counter in range(0,256):
         #fixed_counter = (((counter << 4) & 0xF0) | ((counter >> 4) & 0xF))
         fixed_counter = counter
         opcode = optable[fixed_counter]
         code_indent = ' '*9
         addrmode = addrtable[fixed_counter]
         comment_line = "//%s_%s" % (opcode,addrmode)
   
         print "Counter: ", hex(counter), " >> ", hex(fixed_counter), "  ", comment_line
         if opcode == 'ump':
            continue  
 
         code_line = ""
         if addrmode in ('imp'):
            code_line += "%s%s\n" % (code_indent, native_instr_code_dict.get(opcode, ("//*** Unimplemented ***")))
         elif addrmode == "acc":
            if modify_membyte_instrs.has_key(opcode):
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s %s ***")) % ("savereg","reg_A","reg_A")))
            else:
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s ***")) % ("reg_A",)))
         elif addrmode == "imm":
            code_line += "%seffective_address = reg_PC;\n%sreg_PC++;\n" % (code_indent,code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "im16":
            code_line += "%seffective_address = reg_PC;\n%sreg_PC+=2;\n" % (code_indent,code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "abso":
            code_line += "%seffective_address = read_word_65816(reg_PC);\n%sreg_PC+=2;\n" % (code_indent,code_indent)
            if modify_membyte_instrs.has_key(opcode):
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s %s ***")) % ("write_byte_65816","effective_address","read_byte_65816(effective_address)")))
            else:
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "absx":
            code_line += "%seffective_address = (read_word_65816(reg_PC) + reg_X);\n%sreg_PC+=2;\n" % (code_indent,code_indent)
            if modify_membyte_instrs.has_key(opcode):
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s %s ***")) % ("write_byte_65816","effective_address","read_byte_65816(effective_address)")))
            else:
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "absy":
            code_line += "%seffective_address = (read_word_65816(reg_PC) + reg_Y);\n%sreg_PC+=2;\n" % (code_indent,code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "abind":
            code_line += "%seffective_address = read_word_65816(reg_PC);\n%sreg_PC+=2;\n" % (code_indent,code_indent)
            code_line += "%seffective_address = read_word_65816(effective_address);\n" % (code_indent)
            code_line += "%s%s\n" % (code_indent, native_instr_code_dict.get(opcode, ("//*** Unimplemented ***")))
         elif addrmode == "abindx":
            code_line += "%seffective_address = read_word_65816(reg_PC);\n%sreg_PC+=2;\n" % (code_indent,code_indent)
            code_line += "%seffective_address = (read_word_65816(effective_address) + reg_X);\n" % (code_indent)
            code_line += "%s%s\n" % (code_indent, native_instr_code_dict.get(opcode, ("//*** Unimplemented ***")))
         elif addrmode == "rel":
            code_line += "%seffective_address = sign_extend(read_byte_65816(reg_PC));\n%sreg_PC++;\n" % (code_indent,code_indent)
            code_line += "%s%s\n" % (code_indent, native_instr_code_dict.get(opcode, ("//*** Unimplemented ***")))
         elif addrmode == "zp":
            code_line += "%seffective_address = (uint16_t)read_byte_65816(reg_PC);\n%sreg_PC++;\n" % (code_indent,code_indent)
            if modify_membyte_instrs.has_key(opcode):
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s %s ***")) % ("write_byte_65816","effective_address","read_byte_65816(effective_address)")))
            elif modify_memword_instrs.has_key(opcode):
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s %s ***")) % ("write_word_65816","effective_address","read_word_65816(effective_address)")))
            else:
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "zpx":
            code_line += "%seffective_address = (uint16_t)(((read_byte_65816(reg_PC) + reg_X)) & 0xFF);\n%sreg_PC++;\n" % (code_indent,code_indent)
            if modify_membyte_instrs.has_key(opcode):
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s %s ***")) % ("write_byte_65816","effective_address","read_byte_65816(effective_address)")))
            else:
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "zpy":
            code_line += "%seffective_address = (uint16_t)(((read_byte_65816(reg_PC) + reg_Y)) & 0xFF);\n%sreg_PC++;\n" % (code_indent,code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "inzp":
            code_line += "%seffective_address = (uint16_t)read_byte_65816(reg_PC);\n%sreg_PC++;\n" % (code_indent,code_indent)
            code_line += "%seffective_address = read_word_65816(effective_address);\n" % (code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "indx":
            code_line += "%seffective_address = (uint16_t)(((read_byte_65816(reg_PC) + reg_X)) & 0xFF);\n%sreg_PC++;\n" % (code_indent,code_indent)
            code_line += "%seffective_address = read_word_65816(effective_address);\n" % (code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "indy":
            code_line += "%seffective_address = (uint16_t)read_byte_65816(reg_PC);\n%sreg_PC++;\n" % (code_indent,code_indent)
            code_line += "%seffective_address = (read_word_65816(effective_address) + (uint16_t)reg_Y);\n" % (code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "dW":
            code_line += "%seffective_address = (((uint16_t) read_byte_65816(reg_PC)) + reg_W);\n%sreg_PC++;\n" % (code_indent,code_indent)
            if modify_membyte_instrs.has_key(opcode):
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s %s ***")) % ("write_byte_65816","effective_address","read_byte_65816(effective_address)")))
            else:
               code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         elif addrmode == "indWy":
            code_line += "%seffective_address = (((uint16_t) read_byte_65816(reg_PC)) + reg_W);\n%sreg_PC++;\n" % (code_indent,code_indent)
            code_line += "%seffective_address = (read_word_65816(reg_DB,effective_address) + reg_Y);\n" % (code_indent)
            code_line += "%s%s\n" % (code_indent, (native_instr_code_dict.get(opcode, ("//*** Unimplemented %s %s ***")) % ("65816","effective_address")))
         else:
            print "Invalid address mode: %s" % (addrmode)
            raise SyntaxError            

         code_line += "%sreturn;\n" % (code_indent,)
         code_lines[counter] = "%scase 0x%02X:  %s\n%s" % (' '*7,counter,comment_line,code_line)
   
      for counter in range(0,256):
         if code_lines.has_key(counter):
            f.write(code_lines[counter])
      f.close()



def generate_asm_listings(filename,optable,addrtable):
   opcode_dict = {}
   addrmode_dict = {}
   with open(filename,'w') as f:
      #addrmode_line = ""
      for counter in range(0,256):
         #fixed_counter = (((counter << 4) & 0xF0) | ((counter >> 4) & 0xF))
         fixed_counter = counter
         opcode = optable[fixed_counter]
         code_indent = ' '*3
         addrmode = addrtable[fixed_counter]
         #if (counter % 16) == 0:
         #   print "%s\n" % (addrmode_line,)
         #   addrmode_line = "%02X:%s\t" % (counter,addrtable[counter],)
         #else:
         #   addrmode_line += "%02X:%s\t" % (counter,addrtable[counter],)
         #print "Counter: ", hex(counter), " Fixed Counter: ", hex(fixed_counter), " Addrmode Code: ", addrmode
         if opcode_dict.has_key(opcode):
            opcode_dict[opcode][addrmode] = counter
         else:
            opcode_dict[opcode] = {addrmode: counter}
         addrmode_dict[addrmode] = 0
 
      #Equivalent assembly instructions
      #opcode_dict['pea'] = {}
      #opcode_dict['lea'] = {}
      #opcode_dict['pea']['addr'] = int(opcode_dict['phi']['imm16'])
      #opcode_dict['lea']['addr'] = int(opcode_dict['lda']['imm16'])

      f.write("%sself.instr_info={" % (code_indent * 2,))
      opcode_dict_names = opcode_dict.keys()
      opcode_dict_names.sort()
      for opcode_name in opcode_dict_names:
         f.write("%s'%s':{" % ((code_indent * 8),opcode_name))
         addrmode_counter = 0
         addrmode_names = opcode_dict[opcode_name].keys()
         addrmode_names.sort()
         for addrmode_name in addrmode_names:
            if addrmode_counter == 0:
               f.write("'%s':0x%02X" % (addrmode_name, opcode_dict[opcode_name][addrmode_name]))
            else:
               f.write(",'%s':0x%02X" % (addrmode_name, opcode_dict[opcode_name][addrmode_name]))
            addrmode_counter += 1
         f.write("},\n")         
      f.write("%s}\n" % (code_indent * 2,))        

      f.write("\n%sself.addrmode_list={" % (code_indent * 2,))
      addrmode_dict_names = addrmode_dict.keys()
      addrmode_dict_names.sort()
      for addrmode_name in addrmode_dict_names:
         f.write("%s'%s':{'length':0},\n" % ((code_indent * 9),addrmode_name))
      f.close()


def generate_opcode_documentation(filename,optable,addrtable):
   instruction_list = {}
   address_mode_list = {}
   with open(filename,'w') as f:
      for counter in range(0,256):
         #fixed_counter = (((counter << 4) & 0xF0) | ((counter >> 4) & 0xF))
         fixed_counter = counter
         opcode = optable[fixed_counter]
         code_indent = ' '*9
         addrmode = addrtable[fixed_counter]
         comment_line = "//%s_%s" % (opcode,addrmode)
   
         print "Counter: ", hex(counter), " >> ", hex(fixed_counter), "  ", comment_line
         if opcode == 'ump':
            continue  
         
         if address_mode_list.has_key(addrmode):
            address_mode_list[addrmode] += 1
         else:
            address_mode_list[addrmode] = 0

         if instruction_list.has_key(opcode):
            instruction_list[opcode][addrmode] = counter
         else:
            instruction_list[opcode] = { addrmode: counter }


      addrmode_name_list = address_mode_list.keys()
      addrmode_name_list.sort()
      instr_name_list = instruction_list.keys()
      instr_name_list.sort()
      
      f.write("Instr,")
      for addrmode in addrmode_name_list:
         f.write("%s," % (addrmode,))
      f.write("\n")
      for instr_name in instr_name_list:
         f.write("%s," % (instr_name,))
         for addrmode in addrmode_name_list:
            if instruction_list[instr_name].has_key(addrmode):
               f.write("%02X," % (instruction_list[instr_name][addrmode]))
            else:
               f.write("  ,")
         f.write("\n")   
      f.close()




def generate_opcode_map(filename,optable,addrtable):
   instruction_list = {}
   address_mode_list = {}
   with open(filename,'w') as f:
      f.write("Opcode Map,")   
      for counter in range(0,0x10):
         f.write("%01X," % (counter,)) 
      f.write("\n")   

      for counter in range(0,256):
         #fixed_counter = (((counter << 4) & 0xF0) | ((counter >> 4) & 0xF))
         fixed_counter = counter
         opcode = optable[fixed_counter]
         code_indent = ' '*9
         addrmode = addrtable[fixed_counter]
         comment_line = "//%s_%s" % (opcode,addrmode)
   
         print "Counter: ", hex(counter), " >> ", hex(fixed_counter), "  ", comment_line 
         if((counter & 0xF0) == counter):
            f.write("%02X," % (counter,)) 
         f.write("%s_%s" % (opcode,addrmode,))
         if((counter & 0xF) == 0xF):
            f.write("\n")   
         else:
            f.write(",")   
      f.write("\n")   
      f.close()


#generate_asm_listings()
generate_emulator_case_statement("main_opcodes.txt",main_optable,main_addrtable)
generate_opcode_documentation("opcode_docs.csv",main_optable,main_addrtable)
generate_opcode_map("opcode_map.csv",main_optable,main_addrtable)
generate_asm_listings("asm_code.txt",main_optable,main_addrtable)
#generate_documentation()
