#define dst regbase[r0]
#define imm i1

DEFLABEL(HEAD):
INSN_COUNT0(specconst);Lspecconst0:
TLspecconst:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   specconst dst imm
    dst : destination register
    imm : immediate value of the special constant
   $dst = imm
 */

  dst = imm;
}
#undef dst
#undef imm

