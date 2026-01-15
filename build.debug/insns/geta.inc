#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT0(geta);Lgeta0:
TLgeta:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   geta dst
     dst : destination register
   $dst = a
 */
  dst = get_a(context);
}
#undef dst

