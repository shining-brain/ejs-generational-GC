#define dst regbase[r0]
#define prop v1

DEFLABEL(HEAD):
INSN_COUNT1(getglobal,prop);Lgetglobal0:
TLgetglobal_any:
switch((unsigned int) get_ptag(prop).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(2)
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
case TV_GENERIC:
Lgetglobal1:
TLgetglobal_builtin:
TLgetglobal_special:
TLgetglobal_array:
TLgetglobal_flonum:
TLgetglobal_string_object:
TLgetglobal_boolean_object:
TLgetglobal_simple_object:
TLgetglobal_iterator:
TLgetglobal_regexp:
TLgetglobal_fixnum:
TLgetglobal_number_object:
TLgetglobal_function:
{ // normal_regexp vmgen.newsynth.DecisionDiagram$Leaf@435

  LOG_EXIT("GETGLOBAL with non-stirng variable name\n");
/* Local Variables:      */
/* mode: c               */
/* c-basic-offset: 2     */
/* indent-tabs-mode: nil */
/* End:                  */
}
break;
case TV_STRING:
case 3:
case 2:
case 1:
Lgetglobal2:
TLgetglobal_string:
{ // normal_string vmgen.newsynth.DecisionDiagram$Leaf@1f5d

/*
   getglobal dst reg
     dst : destination register
     reg : register that has a pointer to a string object
   $dst = property value for the string in the global object
 */
#ifdef DEBUG_GETPROP_VALUE
  StringCell *str = jsv_to_normal_string(prop);
  if (trace_flag)
    printf("getglobal: %s\n", str->value);
#endif /* DEBUG_GETPROP_VALUE */
#ifdef INLINE_CACHE
  {
    InlineCache *ic = &insns->inl_cache;
    JSValue obj = context->global;
#ifdef IC_PROF
    ic->count++;
    ic_prof_count++;
    if (ic->prop_name == JS_EMPTY)
      ic->unavailable++;
#endif /* IC_PROF */
    if (ic->prop_name == prop) {
      assert(ic->index <
             object_get_shape(obj)->n_embedded_slots +
             object_get_shape(obj)->n_extension_slots -
             (object_get_shape(obj)->n_extension_slots > 0 ? 1 : 0));
      dst = object_get_prop(obj, ic->index);
      assert(dst != JS_EMPTY);  /* global variable removed? */
#ifdef IC_PROF
      ic->hit++;
      ic_prof_hit++;
#endif /* IC_PROF */
    } else {
      assert(ic->prop_name == JS_EMPTY);
      ic->miss++;
      dst = get_object_prop(context, obj, prop, ic);
      ic->pm = NULL;
    }
  }
#else /* INLINE_CACHE */
  {
    dst = get_prop(context->global, prop);
    if (dst == JS_EMPTY)
      LOG_EXIT("GETGLOBAL: %s not found\n", string_to_cstr(prop));
  }
#endif /* INLINE_CACHE */
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst
#undef prop

