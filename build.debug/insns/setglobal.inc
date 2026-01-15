#define prop v0
#define src v1

DEFLABEL(HEAD):
INSN_COUNT2(setglobal,prop,src);Lsetglobal0:
TLsetglobal_any_string:
TLsetglobal_any_string_object:
TLsetglobal_any_builtin:
TLsetglobal_any_iterator:
TLsetglobal_any_special:
TLsetglobal_any_function:
TLsetglobal_any_fixnum:
TLsetglobal_any_boolean_object:
TLsetglobal_any_array:
TLsetglobal_any_any:
TLsetglobal_any_simple_object:
TLsetglobal_any_flonum:
TLsetglobal_any_regexp:
TLsetglobal_any_number_object:
switch((unsigned int) get_ptag(prop).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(2)
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
case TV_GENERIC:
Lsetglobal1:
TLsetglobal_iterator_number_object:
TLsetglobal_iterator_special:
TLsetglobal_special_number_object:
TLsetglobal_array_string_object:
TLsetglobal_array_array:
TLsetglobal_flonum_string_object:
TLsetglobal_iterator_function:
TLsetglobal_function_iterator:
TLsetglobal_string_object_number_object:
TLsetglobal_fixnum_regexp:
TLsetglobal_iterator_any:
TLsetglobal_array_any:
TLsetglobal_special_function:
TLsetglobal_simple_object_regexp:
TLsetglobal_iterator_builtin:
TLsetglobal_array_fixnum:
TLsetglobal_string_object_array:
TLsetglobal_boolean_object_special:
TLsetglobal_string_object_builtin:
TLsetglobal_regexp_any:
TLsetglobal_simple_object_array:
TLsetglobal_array_string:
TLsetglobal_array_flonum:
TLsetglobal_boolean_object_iterator:
TLsetglobal_array_number_object:
TLsetglobal_function_boolean_object:
TLsetglobal_special_string_object:
TLsetglobal_flonum_flonum:
TLsetglobal_fixnum_special:
TLsetglobal_simple_object_special:
TLsetglobal_string_object_special:
TLsetglobal_boolean_object_builtin:
TLsetglobal_builtin_regexp:
TLsetglobal_special_any:
TLsetglobal_number_object_array:
TLsetglobal_regexp_function:
TLsetglobal_boolean_object_string_object:
TLsetglobal_iterator_regexp:
TLsetglobal_function_flonum:
TLsetglobal_function_special:
TLsetglobal_simple_object_boolean_object:
TLsetglobal_boolean_object_regexp:
TLsetglobal_special_array:
TLsetglobal_builtin_special:
TLsetglobal_regexp_number_object:
TLsetglobal_string_object_flonum:
TLsetglobal_regexp_regexp:
TLsetglobal_fixnum_builtin:
TLsetglobal_boolean_object_boolean_object:
TLsetglobal_special_simple_object:
TLsetglobal_regexp_fixnum:
TLsetglobal_function_string:
TLsetglobal_iterator_string:
TLsetglobal_number_object_boolean_object:
TLsetglobal_builtin_builtin:
TLsetglobal_flonum_regexp:
TLsetglobal_special_fixnum:
TLsetglobal_flonum_iterator:
TLsetglobal_number_object_string_object:
TLsetglobal_string_object_string_object:
TLsetglobal_regexp_flonum:
TLsetglobal_boolean_object_string:
TLsetglobal_flonum_simple_object:
TLsetglobal_flonum_array:
TLsetglobal_function_simple_object:
TLsetglobal_special_flonum:
TLsetglobal_function_builtin:
TLsetglobal_flonum_fixnum:
TLsetglobal_simple_object_number_object:
TLsetglobal_regexp_array:
TLsetglobal_number_object_fixnum:
TLsetglobal_simple_object_iterator:
TLsetglobal_flonum_number_object:
TLsetglobal_number_object_regexp:
TLsetglobal_function_any:
TLsetglobal_regexp_special:
TLsetglobal_fixnum_simple_object:
TLsetglobal_string_object_any:
TLsetglobal_fixnum_number_object:
TLsetglobal_boolean_object_number_object:
TLsetglobal_string_object_string:
TLsetglobal_regexp_builtin:
TLsetglobal_string_object_simple_object:
TLsetglobal_number_object_simple_object:
TLsetglobal_special_boolean_object:
TLsetglobal_fixnum_function:
TLsetglobal_simple_object_fixnum:
TLsetglobal_function_number_object:
TLsetglobal_function_array:
TLsetglobal_string_object_iterator:
TLsetglobal_simple_object_flonum:
TLsetglobal_fixnum_iterator:
TLsetglobal_special_builtin:
TLsetglobal_array_boolean_object:
TLsetglobal_fixnum_string:
TLsetglobal_builtin_flonum:
TLsetglobal_builtin_number_object:
TLsetglobal_boolean_object_array:
TLsetglobal_iterator_iterator:
TLsetglobal_special_iterator:
TLsetglobal_fixnum_flonum:
TLsetglobal_builtin_fixnum:
TLsetglobal_builtin_any:
TLsetglobal_builtin_boolean_object:
TLsetglobal_special_special:
TLsetglobal_flonum_function:
TLsetglobal_boolean_object_function:
TLsetglobal_regexp_simple_object:
TLsetglobal_number_object_number_object:
TLsetglobal_boolean_object_simple_object:
TLsetglobal_builtin_string:
TLsetglobal_iterator_boolean_object:
TLsetglobal_regexp_boolean_object:
TLsetglobal_function_function:
TLsetglobal_string_object_boolean_object:
TLsetglobal_flonum_boolean_object:
TLsetglobal_number_object_function:
TLsetglobal_array_regexp:
TLsetglobal_special_regexp:
TLsetglobal_array_function:
TLsetglobal_string_object_function:
TLsetglobal_simple_object_simple_object:
TLsetglobal_fixnum_string_object:
TLsetglobal_simple_object_any:
TLsetglobal_boolean_object_any:
TLsetglobal_regexp_string:
TLsetglobal_fixnum_boolean_object:
TLsetglobal_builtin_iterator:
TLsetglobal_flonum_special:
TLsetglobal_function_string_object:
TLsetglobal_iterator_fixnum:
TLsetglobal_simple_object_function:
TLsetglobal_flonum_any:
TLsetglobal_string_object_regexp:
TLsetglobal_fixnum_array:
TLsetglobal_boolean_object_fixnum:
TLsetglobal_number_object_any:
TLsetglobal_number_object_special:
TLsetglobal_iterator_flonum:
TLsetglobal_function_regexp:
TLsetglobal_function_fixnum:
TLsetglobal_builtin_simple_object:
TLsetglobal_flonum_string:
TLsetglobal_boolean_object_flonum:
TLsetglobal_special_string:
TLsetglobal_builtin_array:
TLsetglobal_number_object_flonum:
TLsetglobal_iterator_simple_object:
TLsetglobal_array_builtin:
TLsetglobal_iterator_array:
TLsetglobal_iterator_string_object:
TLsetglobal_number_object_iterator:
TLsetglobal_simple_object_builtin:
TLsetglobal_flonum_builtin:
TLsetglobal_simple_object_string_object:
TLsetglobal_array_simple_object:
TLsetglobal_number_object_string:
TLsetglobal_builtin_string_object:
TLsetglobal_fixnum_any:
TLsetglobal_simple_object_string:
TLsetglobal_builtin_function:
TLsetglobal_array_special:
TLsetglobal_fixnum_fixnum:
TLsetglobal_array_iterator:
TLsetglobal_string_object_fixnum:
TLsetglobal_regexp_iterator:
TLsetglobal_regexp_string_object:
TLsetglobal_number_object_builtin:
{ // normal_simple_object normal_regexp vmgen.newsynth.DecisionDiagram$Leaf@16

  LOG_EXIT("SETGLOBAL with non-string variable name\n");
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
Lsetglobal2:
TLsetglobal_string_builtin:
TLsetglobal_string_flonum:
TLsetglobal_string_iterator:
TLsetglobal_string_function:
TLsetglobal_string_array:
TLsetglobal_string_any:
TLsetglobal_string_special:
TLsetglobal_string_fixnum:
TLsetglobal_string_simple_object:
TLsetglobal_string_regexp:
TLsetglobal_string_string_object:
TLsetglobal_string_boolean_object:
TLsetglobal_string_number_object:
TLsetglobal_string_string:
{ // normal_string normal_regexp vmgen.newsynth.DecisionDiagram$Leaf@54

/*
   setglobal s src
     s : register that has a pointer to a string object
     src : property value to be set
   property value for the string in the global object = $src
 */
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
      object_set_prop(obj, ic->index, src);
#ifdef IC_PROF
      ic->hit++;
      ic_prof_hit++;
#endif /* IC_PROF */
    } else {
      assert(ic->prop_name == JS_EMPTY);
      ic->miss++;
      set_prop_with_ic(context, obj, prop, src, ATTR_NONE, ic);
      ic->pm = NULL;
    }
  }
#else /* INLINE_CACHE */
  set_prop(context, context->global, prop, src, ATTR_NONE);
#endif /* INLINE_CACHE */
}
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef prop
#undef src

