#define dst regbase[r0]
#define obj v1

DEFLABEL(HEAD):
INSN_COUNT1(makeiterator,obj);Lmakeiterator0:
TLmakeiterator_string:
TLmakeiterator_builtin:
TLmakeiterator_iterator:
TLmakeiterator_regexp:
TLmakeiterator_flonum:
TLmakeiterator_boolean_object:
TLmakeiterator_number_object:
TLmakeiterator_function:
TLmakeiterator_simple_object:
TLmakeiterator_array:
TLmakeiterator_special:
TLmakeiterator_fixnum:
TLmakeiterator_string_object:
TLmakeiterator_any:
{ // normal_builtin vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   makeiterator obj dst
     dst : destination register
   $dst = iterator object for iterating $obj
 */

  if (!is_object(obj))
    LOG_EXIT("makeiterator: not an object\n");
  dst = new_iterator(context, obj);
}
#undef dst
#undef obj

