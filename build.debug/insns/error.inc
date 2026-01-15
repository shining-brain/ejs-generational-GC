#define dst regbase[r0]
#define d d1

DEFLABEL(HEAD):
INSN_COUNT0(error);Lerror0:
TLerror:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

  dst = 0;
  d = 0;
  return -1;
}
#undef dst
#undef d

