I_FIXNUM:
  ENTER_INSN(__LINE__);
  {
    Register r0 __attribute__((unused)) = get_first_operand_reg(insn);
    SmallPrimitive i1 __attribute__((unused)) =get_small_immediate(insn);
#define USELABEL(x) fixnum_ ## x
#define DEFLABEL(x) fixnum_ ## x
#include "insns/fixnum.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_SPECCONST:
  ENTER_INSN(__LINE__);
  {
    Register r0 __attribute__((unused)) = get_first_operand_reg(insn);
    SmallPrimitive i1 __attribute__((unused)) =get_small_immediate(insn);
#define USELABEL(x) specconst_ ## x
#define DEFLABEL(x) specconst_ ## x
#include "insns/specconst.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_STRING:
I_REGEXP:
I_NUMBER:
  ENTER_INSN(__LINE__);
  {
    Register r0 __attribute__((unused)) = get_first_operand_reg(insn);
    ConstantDisplacement d1 __attribute__((unused)) =get_big_constant_disp(insn);
#define USELABEL(x) number_ ## x
#define DEFLABEL(x) number_ ## x
#include "insns/number.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_ADDREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_any_fixnum;
I_ADDREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_any_flonum;
I_ADDREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_any_string;
I_ADDREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_any_special;
I_ADDFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_fixnum_any;
I_ADDFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_fixnum_flonum;
I_ADDFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_fixnum_string;
I_ADDFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_fixnum_special;
I_ADDFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_flonum_any;
I_ADDFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_flonum_fixnum;
I_ADDFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_flonum_string;
I_ADDFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_flonum_special;
I_ADDSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_string_any;
I_ADDSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_string_fixnum;
I_ADDSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_string_flonum;
I_ADDSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_string_special;
I_ADDSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(add,v1,v2);
    goto TLadd_special_any;
I_ADDSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_special_fixnum;
I_ADDSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_special_flonum;
I_ADDSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(add,v1,v2);
    goto TLadd_special_string;
I_ADD:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) add_ ## x
#define DEFLABEL(x) add_ ## x
#include "insns/add.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_SUBREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_any_fixnum;
I_SUBREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_any_flonum;
I_SUBREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_any_string;
I_SUBREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_any_special;
I_SUBFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_fixnum_any;
I_SUBFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_fixnum_flonum;
I_SUBFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_fixnum_string;
I_SUBFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_fixnum_special;
I_SUBFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_flonum_any;
I_SUBFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_flonum_fixnum;
I_SUBFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_flonum_string;
I_SUBFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_flonum_special;
I_SUBSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_string_any;
I_SUBSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_string_fixnum;
I_SUBSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_string_flonum;
I_SUBSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_string_special;
I_SUBSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_special_any;
I_SUBSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_special_fixnum;
I_SUBSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_special_flonum;
I_SUBSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(sub,v1,v2);
    goto TLsub_special_string;
I_SUB:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) sub_ ## x
#define DEFLABEL(x) sub_ ## x
#include "insns/sub.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_MULREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_any_fixnum;
I_MULREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_any_flonum;
I_MULREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_any_string;
I_MULREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_any_special;
I_MULFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_fixnum_any;
I_MULFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_fixnum_flonum;
I_MULFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_fixnum_string;
I_MULFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_fixnum_special;
I_MULFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_flonum_any;
I_MULFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_flonum_fixnum;
I_MULFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_flonum_string;
I_MULFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_flonum_special;
I_MULSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_string_any;
I_MULSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_string_fixnum;
I_MULSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_string_flonum;
I_MULSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_string_special;
I_MULSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_special_any;
I_MULSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_special_fixnum;
I_MULSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_special_flonum;
I_MULSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mul,v1,v2);
    goto TLmul_special_string;
I_MUL:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) mul_ ## x
#define DEFLABEL(x) mul_ ## x
#include "insns/mul.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_DIVREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_any_fixnum;
I_DIVREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_any_flonum;
I_DIVREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_any_string;
I_DIVREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_any_special;
I_DIVFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_fixnum_any;
I_DIVFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_fixnum_flonum;
I_DIVFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_fixnum_string;
I_DIVFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_fixnum_special;
I_DIVFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_flonum_any;
I_DIVFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_flonum_fixnum;
I_DIVFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_flonum_string;
I_DIVFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_flonum_special;
I_DIVSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_string_any;
I_DIVSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_string_fixnum;
I_DIVSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_string_flonum;
I_DIVSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_string_special;
I_DIVSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_special_any;
I_DIVSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_special_fixnum;
I_DIVSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_special_flonum;
I_DIVSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(div,v1,v2);
    goto TLdiv_special_string;
I_DIV:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) div_ ## x
#define DEFLABEL(x) div_ ## x
#include "insns/div.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_MODREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_any_fixnum;
I_MODREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_any_flonum;
I_MODREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_any_string;
I_MODREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_any_special;
I_MODFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_fixnum_any;
I_MODFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_fixnum_flonum;
I_MODFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_fixnum_string;
I_MODFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_fixnum_special;
I_MODFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_flonum_any;
I_MODFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_flonum_fixnum;
I_MODFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_flonum_string;
I_MODFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_flonum_special;
I_MODSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_string_any;
I_MODSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_string_fixnum;
I_MODSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_string_flonum;
I_MODSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_string_special;
I_MODSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_special_any;
I_MODSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_special_fixnum;
I_MODSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_special_flonum;
I_MODSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(mod,v1,v2);
    goto TLmod_special_string;
I_MOD:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) mod_ ## x
#define DEFLABEL(x) mod_ ## x
#include "insns/mod.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_BITANDREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_any_fixnum;
I_BITANDREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_any_flonum;
I_BITANDREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_any_string;
I_BITANDREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_any_special;
I_BITANDFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_fixnum_any;
I_BITANDFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_fixnum_flonum;
I_BITANDFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_fixnum_string;
I_BITANDFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_fixnum_special;
I_BITANDFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_flonum_any;
I_BITANDFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_flonum_fixnum;
I_BITANDFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_flonum_string;
I_BITANDFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_flonum_special;
I_BITANDSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_string_any;
I_BITANDSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_string_fixnum;
I_BITANDSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_string_flonum;
I_BITANDSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_string_special;
I_BITANDSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_special_any;
I_BITANDSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_special_fixnum;
I_BITANDSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_special_flonum;
I_BITANDSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitand,v1,v2);
    goto TLbitand_special_string;
I_BITAND:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) bitand_ ## x
#define DEFLABEL(x) bitand_ ## x
#include "insns/bitand.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_BITORREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_any_fixnum;
I_BITORREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_any_flonum;
I_BITORREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_any_string;
I_BITORREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_any_special;
I_BITORFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_fixnum_any;
I_BITORFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_fixnum_flonum;
I_BITORFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_fixnum_string;
I_BITORFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_fixnum_special;
I_BITORFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_flonum_any;
I_BITORFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_flonum_fixnum;
I_BITORFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_flonum_string;
I_BITORFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_flonum_special;
I_BITORSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_string_any;
I_BITORSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_string_fixnum;
I_BITORSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_string_flonum;
I_BITORSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_string_special;
I_BITORSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_special_any;
I_BITORSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_special_fixnum;
I_BITORSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_special_flonum;
I_BITORSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(bitor,v1,v2);
    goto TLbitor_special_string;
I_BITOR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) bitor_ ## x
#define DEFLABEL(x) bitor_ ## x
#include "insns/bitor.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_LEFTSHIFTREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_any_fixnum;
I_LEFTSHIFTREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_any_flonum;
I_LEFTSHIFTREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_any_string;
I_LEFTSHIFTREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_any_special;
I_LEFTSHIFTFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_fixnum_any;
I_LEFTSHIFTFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_fixnum_flonum;
I_LEFTSHIFTFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_fixnum_string;
I_LEFTSHIFTFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_fixnum_special;
I_LEFTSHIFTFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_flonum_any;
I_LEFTSHIFTFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_flonum_fixnum;
I_LEFTSHIFTFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_flonum_string;
I_LEFTSHIFTFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_flonum_special;
I_LEFTSHIFTSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_string_any;
I_LEFTSHIFTSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_string_fixnum;
I_LEFTSHIFTSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_string_flonum;
I_LEFTSHIFTSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_string_special;
I_LEFTSHIFTSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_special_any;
I_LEFTSHIFTSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_special_fixnum;
I_LEFTSHIFTSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_special_flonum;
I_LEFTSHIFTSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(leftshift,v1,v2);
    goto TLleftshift_special_string;
I_LEFTSHIFT:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) leftshift_ ## x
#define DEFLABEL(x) leftshift_ ## x
#include "insns/leftshift.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_RIGHTSHIFTREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_any_fixnum;
I_RIGHTSHIFTREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_any_flonum;
I_RIGHTSHIFTREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_any_string;
I_RIGHTSHIFTREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_any_special;
I_RIGHTSHIFTFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_fixnum_any;
I_RIGHTSHIFTFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_fixnum_flonum;
I_RIGHTSHIFTFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_fixnum_string;
I_RIGHTSHIFTFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_fixnum_special;
I_RIGHTSHIFTFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_flonum_any;
I_RIGHTSHIFTFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_flonum_fixnum;
I_RIGHTSHIFTFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_flonum_string;
I_RIGHTSHIFTFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_flonum_special;
I_RIGHTSHIFTSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_string_any;
I_RIGHTSHIFTSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_string_fixnum;
I_RIGHTSHIFTSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_string_flonum;
I_RIGHTSHIFTSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_string_special;
I_RIGHTSHIFTSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_special_any;
I_RIGHTSHIFTSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_special_fixnum;
I_RIGHTSHIFTSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_special_flonum;
I_RIGHTSHIFTSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(rightshift,v1,v2);
    goto TLrightshift_special_string;
I_RIGHTSHIFT:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) rightshift_ ## x
#define DEFLABEL(x) rightshift_ ## x
#include "insns/rightshift.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_UNSIGNEDRIGHTSHIFTREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_any_fixnum;
I_UNSIGNEDRIGHTSHIFTREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_any_flonum;
I_UNSIGNEDRIGHTSHIFTREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_any_string;
I_UNSIGNEDRIGHTSHIFTREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_any_special;
I_UNSIGNEDRIGHTSHIFTFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_fixnum_any;
I_UNSIGNEDRIGHTSHIFTFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_fixnum_flonum;
I_UNSIGNEDRIGHTSHIFTFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_fixnum_string;
I_UNSIGNEDRIGHTSHIFTFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_fixnum_special;
I_UNSIGNEDRIGHTSHIFTFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_flonum_any;
I_UNSIGNEDRIGHTSHIFTFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_flonum_fixnum;
I_UNSIGNEDRIGHTSHIFTFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_flonum_string;
I_UNSIGNEDRIGHTSHIFTFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_flonum_special;
I_UNSIGNEDRIGHTSHIFTSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_string_any;
I_UNSIGNEDRIGHTSHIFTSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_string_fixnum;
I_UNSIGNEDRIGHTSHIFTSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_string_flonum;
I_UNSIGNEDRIGHTSHIFTSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_string_special;
I_UNSIGNEDRIGHTSHIFTSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_special_any;
I_UNSIGNEDRIGHTSHIFTSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_special_fixnum;
I_UNSIGNEDRIGHTSHIFTSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_special_flonum;
I_UNSIGNEDRIGHTSHIFTSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(unsignedrightshift,v1,v2);
    goto TLunsignedrightshift_special_string;
I_UNSIGNEDRIGHTSHIFT:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) unsignedrightshift_ ## x
#define DEFLABEL(x) unsignedrightshift_ ## x
#include "insns/unsignedrightshift.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_LESSTHANREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_any_fixnum;
I_LESSTHANREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_any_flonum;
I_LESSTHANREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_any_string;
I_LESSTHANREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_any_special;
I_LESSTHANFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_fixnum_any;
I_LESSTHANFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_fixnum_flonum;
I_LESSTHANFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_fixnum_string;
I_LESSTHANFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_fixnum_special;
I_LESSTHANFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_flonum_any;
I_LESSTHANFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_flonum_fixnum;
I_LESSTHANFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_flonum_string;
I_LESSTHANFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_flonum_special;
I_LESSTHANSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_string_any;
I_LESSTHANSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_string_fixnum;
I_LESSTHANSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_string_flonum;
I_LESSTHANSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_string_special;
I_LESSTHANSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_special_any;
I_LESSTHANSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_special_fixnum;
I_LESSTHANSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_special_flonum;
I_LESSTHANSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthan,v1,v2);
    goto TLlessthan_special_string;
I_LESSTHAN:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) lessthan_ ## x
#define DEFLABEL(x) lessthan_ ## x
#include "insns/lessthan.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_LESSTHANEQUALREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_any_fixnum;
I_LESSTHANEQUALREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_any_flonum;
I_LESSTHANEQUALREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_any_string;
I_LESSTHANEQUALREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_any_special;
I_LESSTHANEQUALFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_fixnum_any;
I_LESSTHANEQUALFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_fixnum_flonum;
I_LESSTHANEQUALFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_fixnum_string;
I_LESSTHANEQUALFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_fixnum_special;
I_LESSTHANEQUALFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_flonum_any;
I_LESSTHANEQUALFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_flonum_fixnum;
I_LESSTHANEQUALFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_flonum_string;
I_LESSTHANEQUALFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_flonum_special;
I_LESSTHANEQUALSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_string_any;
I_LESSTHANEQUALSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_string_fixnum;
I_LESSTHANEQUALSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_string_flonum;
I_LESSTHANEQUALSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_string_special;
I_LESSTHANEQUALSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_special_any;
I_LESSTHANEQUALSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_special_fixnum;
I_LESSTHANEQUALSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_special_flonum;
I_LESSTHANEQUALSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(lessthanequal,v1,v2);
    goto TLlessthanequal_special_string;
I_LESSTHANEQUAL:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) lessthanequal_ ## x
#define DEFLABEL(x) lessthanequal_ ## x
#include "insns/lessthanequal.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_EQREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_any_fixnum;
I_EQREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_any_flonum;
I_EQREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_any_string;
I_EQREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_any_special;
I_EQFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_fixnum_any;
I_EQFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_fixnum_flonum;
I_EQFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_fixnum_string;
I_EQFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_fixnum_special;
I_EQFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_flonum_any;
I_EQFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_flonum_fixnum;
I_EQFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_flonum_string;
I_EQFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_flonum_special;
I_EQSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_string_any;
I_EQSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_string_fixnum;
I_EQSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_string_flonum;
I_EQSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_string_special;
I_EQSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_special_any;
I_EQSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_special_fixnum;
I_EQSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_special_flonum;
I_EQSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(eq,v1,v2);
    goto TLeq_special_string;
I_EQ:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) eq_ ## x
#define DEFLABEL(x) eq_ ## x
#include "insns/eq.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_EQUALREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_any_fixnum;
I_EQUALREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_any_flonum;
I_EQUALREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_any_string;
I_EQUALREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_any_special;
I_EQUALFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_fixnum_any;
I_EQUALFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_fixnum_flonum;
I_EQUALFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_fixnum_string;
I_EQUALFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_fixnum_special;
I_EQUALFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_flonum_any;
I_EQUALFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_flonum_fixnum;
I_EQUALFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_flonum_string;
I_EQUALFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_flonum_special;
I_EQUALSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_string_any;
I_EQUALSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_string_fixnum;
I_EQUALSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_string_flonum;
I_EQUALSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_string_special;
I_EQUALSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_special_any;
I_EQUALSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_special_fixnum;
I_EQUALSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_special_flonum;
I_EQUALSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(equal,v1,v2);
    goto TLequal_special_string;
I_EQUAL:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) equal_ ## x
#define DEFLABEL(x) equal_ ## x
#include "insns/equal.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    int i1 __attribute__((unused));
    Subscript s2 __attribute__((unused));
I_GETARG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    i1 = get_second_operand_int(insn);
    s2 = get_third_operand_subscr(insn);
#define USELABEL(x) getarg_ ## x
#define DEFLABEL(x) getarg_ ## x
#include "insns/getarg.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    int i0 __attribute__((unused));
    Subscript s1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_SETARG:
  ENTER_INSN(__LINE__);
    i0 = get_first_operand_int(insn);
    s1 = get_second_operand_subscr(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) setarg_ ## x
#define DEFLABEL(x) setarg_ ## x
#include "insns/setarg.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_GETPROPREGFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_any_fixnum;
I_GETPROPREGFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_any_flonum;
I_GETPROPREGSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_any_string;
I_GETPROPREGSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_any_special;
I_GETPROPFIXREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_fixnum_any;
I_GETPROPFIXFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_fixnum_flonum;
I_GETPROPFIXSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_fixnum_string;
I_GETPROPFIXSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_fixnum_special;
I_GETPROPFLOREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_flonum_any;
I_GETPROPFLOFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_flonum_fixnum;
I_GETPROPFLOSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_flonum_string;
I_GETPROPFLOSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_flonum_special;
I_GETPROPSTRREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_string_any;
I_GETPROPSTRFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_string_fixnum;
I_GETPROPSTRFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_string_flonum;
I_GETPROPSTRSPEC:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_string_special;
I_GETPROPSPECREG:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_special_any;
I_GETPROPSPECFIX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_special_fixnum;
I_GETPROPSPECFLO:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_special_flonum;
I_GETPROPSPECSTR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT2(getprop,v1,v2);
    goto TLgetprop_special_string;
I_GETPROP:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) getprop_ ## x
#define DEFLABEL(x) getprop_ ## x
#include "insns/getprop.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    JSValue v0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_SETPROPREGREGFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_any_fixnum;
I_SETPROPREGREGFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_any_flonum;
I_SETPROPREGREGSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_any_string;
I_SETPROPREGREGSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_any_special;
I_SETPROPREGFIXREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_fixnum_any;
I_SETPROPREGFIXFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_fixnum_fixnum;
I_SETPROPREGFIXFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_fixnum_flonum;
I_SETPROPREGFIXSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_fixnum_string;
I_SETPROPREGFIXSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_fixnum_special;
I_SETPROPREGFLOREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_flonum_any;
I_SETPROPREGFLOFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_flonum_fixnum;
I_SETPROPREGFLOFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_flonum_flonum;
I_SETPROPREGFLOSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_flonum_string;
I_SETPROPREGFLOSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_flonum_special;
I_SETPROPREGSTRREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_string_any;
I_SETPROPREGSTRFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_string_fixnum;
I_SETPROPREGSTRFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_string_flonum;
I_SETPROPREGSTRSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_string_string;
I_SETPROPREGSTRSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_string_special;
I_SETPROPREGSPECREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_special_any;
I_SETPROPREGSPECFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_special_fixnum;
I_SETPROPREGSPECFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_special_flonum;
I_SETPROPREGSPECSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_special_string;
I_SETPROPREGSPECSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_any_special_special;
I_SETPROPFIXREGREG:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_any_any;
I_SETPROPFIXREGFIX:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_any_fixnum;
I_SETPROPFIXREGFLO:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_any_flonum;
I_SETPROPFIXREGSTR:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_any_string;
I_SETPROPFIXREGSPEC:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_any_special;
I_SETPROPFIXFIXREG:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_fixnum_any;
I_SETPROPFIXFIXFIX:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_fixnum_fixnum;
I_SETPROPFIXFIXFLO:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_fixnum_flonum;
I_SETPROPFIXFIXSTR:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_fixnum_string;
I_SETPROPFIXFIXSPEC:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_fixnum_special;
I_SETPROPFIXFLOREG:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_flonum_any;
I_SETPROPFIXFLOFIX:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_flonum_fixnum;
I_SETPROPFIXFLOFLO:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_flonum_flonum;
I_SETPROPFIXFLOSTR:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_flonum_string;
I_SETPROPFIXFLOSPEC:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_flonum_special;
I_SETPROPFIXSTRREG:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_string_any;
I_SETPROPFIXSTRFIX:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_string_fixnum;
I_SETPROPFIXSTRFLO:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_string_flonum;
I_SETPROPFIXSTRSTR:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_string_string;
I_SETPROPFIXSTRSPEC:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_string_special;
I_SETPROPFIXSPECREG:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_special_any;
I_SETPROPFIXSPECFIX:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_special_fixnum;
I_SETPROPFIXSPECFLO:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_special_flonum;
I_SETPROPFIXSPECSTR:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_special_string;
I_SETPROPFIXSPECSPEC:
  ENTER_INSN(__LINE__);
    v0 = small_cint_to_fixnum(get_first_operand_int(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_fixnum_special_special;
I_SETPROPFLOREGREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_any_any;
I_SETPROPFLOREGFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_any_fixnum;
I_SETPROPFLOREGFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_any_flonum;
I_SETPROPFLOREGSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_any_string;
I_SETPROPFLOREGSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_any_special;
I_SETPROPFLOFIXREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_fixnum_any;
I_SETPROPFLOFIXFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_fixnum_fixnum;
I_SETPROPFLOFIXFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_fixnum_flonum;
I_SETPROPFLOFIXSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_fixnum_string;
I_SETPROPFLOFIXSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_fixnum_special;
I_SETPROPFLOFLOREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_flonum_any;
I_SETPROPFLOFLOFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_flonum_fixnum;
I_SETPROPFLOFLOFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_flonum_flonum;
I_SETPROPFLOFLOSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_flonum_string;
I_SETPROPFLOFLOSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_flonum_special;
I_SETPROPFLOSTRREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_string_any;
I_SETPROPFLOSTRFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_string_fixnum;
I_SETPROPFLOSTRFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_string_flonum;
I_SETPROPFLOSTRSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_string_string;
I_SETPROPFLOSTRSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_string_special;
I_SETPROPFLOSPECREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_special_any;
I_SETPROPFLOSPECFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_special_fixnum;
I_SETPROPFLOSPECFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_special_flonum;
I_SETPROPFLOSPECSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_special_string;
I_SETPROPFLOSPECSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_flonum_special_special;
I_SETPROPSTRREGREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_any_any;
I_SETPROPSTRREGFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_any_fixnum;
I_SETPROPSTRREGFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_any_flonum;
I_SETPROPSTRREGSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_any_string;
I_SETPROPSTRREGSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_any_special;
I_SETPROPSTRFIXREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_fixnum_any;
I_SETPROPSTRFIXFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_fixnum_fixnum;
I_SETPROPSTRFIXFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_fixnum_flonum;
I_SETPROPSTRFIXSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_fixnum_string;
I_SETPROPSTRFIXSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_fixnum_special;
I_SETPROPSTRFLOREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_flonum_any;
I_SETPROPSTRFLOFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_flonum_fixnum;
I_SETPROPSTRFLOFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_flonum_flonum;
I_SETPROPSTRFLOSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_flonum_string;
I_SETPROPSTRFLOSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_flonum_special;
I_SETPROPSTRSTRREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_string_any;
I_SETPROPSTRSTRFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_string_fixnum;
I_SETPROPSTRSTRFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_string_flonum;
I_SETPROPSTRSTRSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_string_string;
I_SETPROPSTRSTRSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_string_special;
I_SETPROPSTRSPECREG:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_special_any;
I_SETPROPSTRSPECFIX:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_special_fixnum;
I_SETPROPSTRSPECFLO:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_special_flonum;
I_SETPROPSTRSPECSTR:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_special_string;
I_SETPROPSTRSPECSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_literal(insns, get_first_operand_constant_disp(insn));
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_string_special_special;
I_SETPROPSPECREGREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_any_any;
I_SETPROPSPECREGFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_value(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_any_fixnum;
I_SETPROPSPECREGFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_any_flonum;
I_SETPROPSPECREGSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_any_string;
I_SETPROPSPECREGSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_any_special;
I_SETPROPSPECFIXREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_fixnum_any;
I_SETPROPSPECFIXFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_fixnum_fixnum;
I_SETPROPSPECFIXFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_fixnum_flonum;
I_SETPROPSPECFIXSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_fixnum_string;
I_SETPROPSPECFIXSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = small_cint_to_fixnum(get_second_operand_int(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_fixnum_special;
I_SETPROPSPECFLOREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_flonum_any;
I_SETPROPSPECFLOFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_flonum_fixnum;
I_SETPROPSPECFLOFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_flonum_flonum;
I_SETPROPSPECFLOSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_flonum_string;
I_SETPROPSPECFLOSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_flonum_special;
I_SETPROPSPECSTRREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_string_any;
I_SETPROPSPECSTRFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_string_fixnum;
I_SETPROPSPECSTRFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_string_flonum;
I_SETPROPSPECSTRSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_string_string;
I_SETPROPSPECSTRSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_literal(insns, get_second_operand_constant_disp(insn));
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_string_special;
I_SETPROPSPECSPECREG:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_value(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_special_any;
I_SETPROPSPECSPECFIX:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_int(insn);
    v2 = small_cint_to_fixnum(get_third_operand_int(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_special_fixnum;
I_SETPROPSPECSPECFLO:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_special_flonum;
I_SETPROPSPECSPECSTR:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_literal(insns, get_third_operand_constant_disp(insn));
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_special_string;
I_SETPROPSPECSPECSPEC:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_int(insn);
    v1 = get_second_operand_int(insn);
    v2 = get_third_operand_int(insn);
    INSN_COUNT3(setprop,v0,v1,v2);
    goto TLsetprop_special_special_special;
I_SETPROP:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) setprop_ ## x
#define DEFLABEL(x) setprop_ ## x
#include "insns/setprop.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_GETGLOBAL:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) getglobal_ ## x
#define DEFLABEL(x) getglobal_ ## x
#include "insns/getglobal.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    JSValue v0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_SETGLOBAL:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) setglobal_ ## x
#define DEFLABEL(x) setglobal_ ## x
#include "insns/setglobal.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
    JSValue v2 __attribute__((unused));
I_INSTANCEOF:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
    v2 = get_third_operand_value(insn);
#define USELABEL(x) instanceof_ ## x
#define DEFLABEL(x) instanceof_ ## x
#include "insns/instanceof.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_MOVE:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) move_ ## x
#define DEFLABEL(x) move_ ## x
#include "insns/move.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    Register r1 __attribute__((unused));
I_TYPEOF:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    r1 = get_second_operand_reg(insn);
#define USELABEL(x) typeof_ ## x
#define DEFLABEL(x) typeof_ ## x
#include "insns/typeof.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_NOT:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) not_ ## x
#define DEFLABEL(x) not_ ## x
#include "insns/not.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_NEW:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) new_ ## x
#define DEFLABEL(x) new_ ## x
#include "insns/new.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_ISUNDEF:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) isundef_ ## x
#define DEFLABEL(x) isundef_ ## x
#include "insns/isundef.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_ISOBJECT:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) isobject_ ## x
#define DEFLABEL(x) isobject_ ## x
#include "insns/isobject.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    int i0 __attribute__((unused));
I_SETFL:
  ENTER_INSN(__LINE__);
    i0 = get_first_operand_int(insn);
#define USELABEL(x) setfl_ ## x
#define DEFLABEL(x) setfl_ ## x
#include "insns/setfl.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    JSValue v0 __attribute__((unused));
I_SETA:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
#define USELABEL(x) seta_ ## x
#define DEFLABEL(x) seta_ ## x
#include "insns/seta.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
I_GETA:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
#define USELABEL(x) geta_ ## x
#define DEFLABEL(x) geta_ ## x
#include "insns/geta.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
I_GETERR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
#define USELABEL(x) geterr_ ## x
#define DEFLABEL(x) geterr_ ## x
#include "insns/geterr.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
I_GETGLOBALOBJ:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
#define USELABEL(x) getglobalobj_ ## x
#define DEFLABEL(x) getglobalobj_ ## x
#include "insns/getglobalobj.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    int i0 __attribute__((unused));
    int i1 __attribute__((unused));
I_NEWFRAME:
  ENTER_INSN(__LINE__);
    i0 = get_first_operand_int(insn);
    i1 = get_second_operand_int(insn);
#define USELABEL(x) newframe_ ## x
#define DEFLABEL(x) newframe_ ## x
#include "insns/newframe.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
I_EXITFRAME:
  ENTER_INSN(__LINE__);
#define USELABEL(x) exitframe_ ## x
#define DEFLABEL(x) exitframe_ ## x
#include "insns/exitframe.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
I_RET:
  ENTER_INSN(__LINE__);
#define USELABEL(x) ret_ ## x
#define DEFLABEL(x) ret_ ## x
#include "insns/ret.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
I_NOP:
  ENTER_INSN(__LINE__);
#define USELABEL(x) nop_ ## x
#define DEFLABEL(x) nop_ ## x
#include "insns/nop.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

I_JUMP:
  ENTER_INSN(__LINE__);
  {
    InstructionDisplacement d0 = get_operand_instruction_disp(insn);
#define USELABEL(x) jump_ ## x
#define DEFLABEL(x) jump_ ## x
#include "insns/jump.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_NOINCPC();

I_JUMPTRUE:
  ENTER_INSN(__LINE__);
  {
    JSValue v0 __attribute__((unused)) = get_first_operand_value(insn);
    InstructionDisplacement d1 = get_operand_instruction_disp(insn);
#define USELABEL(x) jumptrue_ ## x
#define DEFLABEL(x) jumptrue_ ## x
#include "insns/jumptrue.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_JUMPFALSE:
  ENTER_INSN(__LINE__);
  {
    JSValue v0 __attribute__((unused)) = get_first_operand_value(insn);
    InstructionDisplacement d1 = get_operand_instruction_disp(insn);
#define USELABEL(x) jumpfalse_ ## x
#define DEFLABEL(x) jumpfalse_ ## x
#include "insns/jumpfalse.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_GETLOCAL:
  ENTER_INSN(__LINE__);
  {
    Register r0 __attribute__((unused)) = get_first_operand_reg(insn);
    int i1 __attribute__((unused)) = get_second_operand_int(insn);
    Subscript s2 __attribute__((unused)) = get_third_operand_subscr(insn);
#define USELABEL(x) getlocal_ ## x
#define DEFLABEL(x) getlocal_ ## x
#include "insns/getlocal.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_SETLOCAL:
  ENTER_INSN(__LINE__);
  {
    int i0 __attribute__((unused)) = get_first_operand_int(insn);
    Subscript s1 __attribute__((unused)) = get_second_operand_subscr(insn);
    JSValue v2 __attribute__((unused)) = get_third_operand_value(insn);
#define USELABEL(x) setlocal_ ## x
#define DEFLABEL(x) setlocal_ ## x
#include "insns/setlocal.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_MAKECLOSURE:
  ENTER_INSN(__LINE__);
  {
    Register r0 __attribute__((unused)) = get_first_operand_reg(insn);
    Subscript s1 __attribute__((unused)) = get_second_operand_subscr(insn);
#define USELABEL(x) makeclosure_ ## x
#define DEFLABEL(x) makeclosure_ ## x
#include "insns/makeclosure.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_MAKEITERATOR:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) makeiterator_ ## x
#define DEFLABEL(x) makeiterator_ ## x
#include "insns/makeiterator.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    Register r0 __attribute__((unused));
    JSValue v1 __attribute__((unused));
I_NEXTPROPNAMEIDX:
  ENTER_INSN(__LINE__);
    r0 = get_first_operand_reg(insn);
    v1 = get_second_operand_value(insn);
#define USELABEL(x) nextpropnameidx_ ## x
#define DEFLABEL(x) nextpropnameidx_ ## x
#include "insns/nextpropnameidx.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

I_SEND:
I_NEWSEND:
I_CALL:
  ENTER_INSN(__LINE__);
  {
    JSValue v0 __attribute__((unused)) = get_first_operand_value(insn);
    int i1 __attribute__((unused)) = get_second_operand_int(insn);
#define USELABEL(x) call_ ## x
#define DEFLABEL(x) call_ ## x
#include "insns/call.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_TAILSEND:
I_TAILCALL:
  ENTER_INSN(__LINE__);
  {
    JSValue v0 __attribute__((unused)) = get_first_operand_value(insn);
    int i1 __attribute__((unused)) = get_second_operand_int(insn);
#define USELABEL(x) tailcall_ ## x
#define DEFLABEL(x) tailcall_ ## x
#include "insns/tailcall.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_PUSHHANDLER:
  ENTER_INSN(__LINE__);
  {
    InstructionDisplacement d0 = get_operand_instruction_disp(insn);
#define USELABEL(x) pushhandler_ ## x
#define DEFLABEL(x) pushhandler_ ## x
#include "insns/pushhandler.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

  {
I_POPHANDLER:
  ENTER_INSN(__LINE__);
#define USELABEL(x) pophandler_ ## x
#define DEFLABEL(x) pophandler_ ## x
#include "insns/pophandler.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
    JSValue v0 __attribute__((unused));
I_THROW:
  ENTER_INSN(__LINE__);
    v0 = get_first_operand_value(insn);
#define USELABEL(x) throw_ ## x
#define DEFLABEL(x) throw_ ## x
#include "insns/throw.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_NOINCPC();
  }

I_LOCALCALL:
  ENTER_INSN(__LINE__);
  {
    InstructionDisplacement d0 = get_operand_instruction_disp(insn);
#define USELABEL(x) localcall_ ## x
#define DEFLABEL(x) localcall_ ## x
#include "insns/localcall.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_NOINCPC();

  {
I_LOCALRET:
  ENTER_INSN(__LINE__);
#define USELABEL(x) localret_ ## x
#define DEFLABEL(x) localret_ ## x
#include "insns/localret.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

  {
I_POPLOCAL:
  ENTER_INSN(__LINE__);
#define USELABEL(x) poplocal_ ## x
#define DEFLABEL(x) poplocal_ ## x
#include "insns/poplocal.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

I_ERROR:
  ENTER_INSN(__LINE__);
  {
    Register r0 __attribute__((unused)) = get_first_operand_reg(insn);
    ConstantDisplacement d1 __attribute__((unused)) =get_big_constant_disp(insn);
#define USELABEL(x) error_ ## x
#define DEFLABEL(x) error_ ## x
#include "insns/error.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

I_UNKNOWN:
  ENTER_INSN(__LINE__);
  {
#define USELABEL(x) unknown_ ## x
#define DEFLABEL(x) unknown_ ## x
#include "insns/unknown.inc"
#undef USELABEL
#undef DEFLABEL
  }
  NEXT_INSN_INCPC();

  {
I_END:
  ENTER_INSN(__LINE__);
#define USELABEL(x) end_ ## x
#define DEFLABEL(x) end_ ## x
#include "insns/end.inc"
#undef USELABEL
#undef DEFLABEL
  NEXT_INSN_INCPC();
  }

