#define dst regbase[r0]
#define src v1

DEFLABEL(HEAD):
INSN_COUNT1(move,src);Lmove0:
TLmove_boolean_object:
TLmove_string:
TLmove_regexp:
TLmove_fixnum:
TLmove_iterator:
TLmove_function:
TLmove_builtin:
TLmove_array:
TLmove_special:
TLmove_any:
TLmove_flonum:
TLmove_number_object:
TLmove_string_object:
TLmove_simple_object:
{ // normal_builtin vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   move dst src
     dst : destination register
     src : source register
   $dst = $src
 */
  dst = src;
}
#undef dst
#undef src

