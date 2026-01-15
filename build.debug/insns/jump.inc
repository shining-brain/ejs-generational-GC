#define d d0

DEFLABEL(HEAD):
INSN_COUNT0(jump);Ljump0:
TLjump:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   jump disp
   pc = pc + $disp
 */

  set_pc_relative(d);
}
#undef d

