#pragma once

namespace core {

typedef enum {
  boole_clr = 0,
  boole_and = 1,
  boole_andc2 = 2,
  boole_1 = 3,
  boole_andc1 = 4,
  boole_2 = 5,
  boole_xor = 6,
  boole_ior = 7,
  boole_nor = 8,
  boole_eqv = 9,
  boole_c2 = 10,
  boole_orc2 = 11,
  boole_c1 = 12,
  boole_orc1 = 13,
  boole_nand = 14,
  boole_set = 15
} boole_ops;

#define boolOpsMax 16

void initialize_bits();

Integer_sp cl__logior(List_sp integers);
Integer_sp core__logior_2op(Integer_sp, Integer_sp);
Integer_sp cl__logand(List_sp integers);
Integer_sp core__logand_2op(Integer_sp, Integer_sp);
Integer_sp cl__logandc2(Integer_sp, Integer_sp);
Integer_sp cl__lognot(Integer_sp);
bool cl__logbitp(Integer_sp, Integer_sp);

}; // namespace core
