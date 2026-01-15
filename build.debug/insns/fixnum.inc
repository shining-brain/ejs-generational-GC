#define dst regbase[r0]
#define num i1

DEFLABEL(HEAD):
INSN_COUNT0(fixnum);Lfixnum0:
TLfixnum:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
  fixnum dst imm
  dst : destination register
  imm : immediate value of the fixnum
  $dst = imm
 */

  dst = small_cint_to_fixnum((cint) num);
}
#undef dst
#undef num

