#define dst regbase[r0]

DEFLABEL(HEAD):
INSN_COUNT0(getglobalobj);Lgetglobalobj0:
TLgetglobalobj:
{ // vmgen.newsynth.DecisionDiagram$Leaf@550

/*
   getglobalobj dst
   $dst = global object
 */ 

 dst = context->global;
}
#undef dst

