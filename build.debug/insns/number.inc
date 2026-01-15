#define dst regbase[r0]
#define disp d1

DEFLABEL(HEAD):
INSN_COUNT0(number);Lnumber0:
TLnumber:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   number dst disp
   string dst disp
   regexp dst disp
     dst : destination register
     disp : displacement of the constant position from the pc
   $dst = ((JSValur *)insns)[disp]
 */

  // dst = insns[disp].code;
  dst = get_literal(insns, disp);
}
#undef dst
#undef disp

