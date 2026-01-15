#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT0(geterr);Lgeterr0:
TLgeterr:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   geterr dst
     dst : destination register
   $dst = err
   I don't know in which situation this instruction is necessary.
 */ 

 dst = get_err(context);
}
#undef dst

