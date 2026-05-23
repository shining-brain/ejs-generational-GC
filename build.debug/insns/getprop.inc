#define dst regbase[r0]
#define obj v1
#define prop v2

DEFLABEL(HEAD):
INSN_COUNT2(getprop,obj,prop);Lgetprop0:
TLgetprop_function_any:
TLgetprop_any_any:
TLgetprop_builtin_any:
TLgetprop_string_object_any:
TLgetprop_special_any:
TLgetprop_regexp_any:
TLgetprop_flonum_any:
TLgetprop_iterator_any:
TLgetprop_boolean_object_any:
TLgetprop_number_object_any:
TLgetprop_string_any:
TLgetprop_fixnum_any:
TLgetprop_array_any:
TLgetprop_simple_object_any:
switch((unsigned int) get_ptag(prop).v){ // vmgen.newsynth.DecisionDiagram$PTNode@550(3)
case TV_FIXNUM:
default:
Lgetprop1:
TLgetprop_any_fixnum:
switch((unsigned int) get_ptag(obj).v){ // vmgen.newsynth.DecisionDiagram$PTNode@e18(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
Lgetprop2:
TLgetprop_string_boolean_object:
TLgetprop_fixnum_special:
TLgetprop_flonum_boolean_object:
TLgetprop_special_flonum:
TLgetprop_fixnum_string_object:
TLgetprop_special_string_object:
TLgetprop_string_iterator:
TLgetprop_fixnum_function:
TLgetprop_string_builtin:
TLgetprop_special_function:
TLgetprop_special_special:
TLgetprop_fixnum_boolean_object:
TLgetprop_iterator_special:
TLgetprop_iterator_simple_object:
TLgetprop_fixnum_flonum:
TLgetprop_flonum_number_object:
TLgetprop_fixnum_iterator:
TLgetprop_iterator_string:
TLgetprop_special_simple_object:
TLgetprop_string_number_object:
TLgetprop_iterator_builtin:
TLgetprop_iterator_fixnum:
TLgetprop_special_builtin:
TLgetprop_string_array:
TLgetprop_special_string:
TLgetprop_special_array:
TLgetprop_special_fixnum:
TLgetprop_fixnum_array:
TLgetprop_fixnum_number_object:
TLgetprop_special_regexp:
TLgetprop_iterator_number_object:
TLgetprop_flonum_flonum:
TLgetprop_iterator_regexp:
TLgetprop_flonum_array:
TLgetprop_iterator_string_object:
TLgetprop_iterator_array:
TLgetprop_string_string:
TLgetprop_iterator_function:
TLgetprop_string_simple_object:
TLgetprop_flonum_fixnum:
TLgetprop_string_fixnum:
TLgetprop_string_regexp:
TLgetprop_flonum_string_object:
TLgetprop_flonum_regexp:
TLgetprop_flonum_function:
TLgetprop_flonum_iterator:
TLgetprop_iterator_flonum:
TLgetprop_flonum_special:
TLgetprop_iterator_iterator:
TLgetprop_fixnum_string:
TLgetprop_string_special:
TLgetprop_fixnum_builtin:
TLgetprop_flonum_string:
TLgetprop_special_iterator:
TLgetprop_fixnum_fixnum:
TLgetprop_fixnum_simple_object:
TLgetprop_special_boolean_object:
TLgetprop_string_function:
TLgetprop_string_flonum:
TLgetprop_flonum_simple_object:
TLgetprop_iterator_boolean_object:
TLgetprop_string_string_object:
TLgetprop_flonum_builtin:
TLgetprop_special_number_object:
TLgetprop_fixnum_regexp:
{ // normal_flonum normal_iterator vmgen.newsynth.DecisionDiagram$Leaf@4e

  if (obj == JS_UNDEFINED)
     LOG_EXIT("GETPROP: attempt to get property of `undefined'\n");
  GC_PUSH(prop);
  obj = to_object(context, obj);
  GC_POP(prop);
  goto USELABEL(HEAD);
/* Local Variables:      */
/* mode: c               */
/* c-basic-offset: 2     */
/* indent-tabs-mode: nil */
/* End:                  */
}
break;
case TV_GENERIC:
Lgetprop3:switch((unsigned int) get_htag(obj).v){ // vmgen.newsynth.DecisionDiagram$HTNode@2609(3)
case HTAGV_ITERATOR:
goto Lgetprop2;
break;
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_FUNCTION:
case HTAGV_BUILTIN:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
Lgetprop4:
TLgetprop_function_array:
TLgetprop_regexp_number_object:
TLgetprop_string_object_flonum:
TLgetprop_string_object_array:
TLgetprop_boolean_object_regexp:
TLgetprop_simple_object_special:
TLgetprop_builtin_array:
TLgetprop_string_object_special:
TLgetprop_boolean_object_builtin:
TLgetprop_simple_object_string_object:
TLgetprop_string_object_regexp:
TLgetprop_regexp_special:
TLgetprop_builtin_string_object:
TLgetprop_number_object_array:
TLgetprop_regexp_builtin:
TLgetprop_number_object_builtin:
TLgetprop_builtin_boolean_object:
TLgetprop_regexp_boolean_object:
TLgetprop_boolean_object_number_object:
TLgetprop_boolean_object_special:
TLgetprop_string_object_fixnum:
TLgetprop_boolean_object_fixnum:
TLgetprop_builtin_flonum:
TLgetprop_function_simple_object:
TLgetprop_number_object_fixnum:
TLgetprop_number_object_regexp:
TLgetprop_regexp_regexp:
TLgetprop_regexp_string_object:
TLgetprop_number_object_number_object:
TLgetprop_builtin_number_object:
TLgetprop_number_object_flonum:
TLgetprop_builtin_fixnum:
TLgetprop_regexp_flonum:
TLgetprop_string_object_builtin:
TLgetprop_simple_object_builtin:
TLgetprop_number_object_simple_object:
TLgetprop_simple_object_function:
TLgetprop_number_object_special:
TLgetprop_function_boolean_object:
TLgetprop_string_object_simple_object:
TLgetprop_boolean_object_flonum:
TLgetprop_function_string_object:
TLgetprop_number_object_function:
TLgetprop_string_object_function:
TLgetprop_regexp_array:
TLgetprop_function_flonum:
TLgetprop_regexp_function:
TLgetprop_builtin_regexp:
TLgetprop_function_builtin:
TLgetprop_simple_object_flonum:
TLgetprop_simple_object_fixnum:
TLgetprop_simple_object_boolean_object:
TLgetprop_regexp_simple_object:
TLgetprop_builtin_function:
TLgetprop_simple_object_regexp:
TLgetprop_builtin_iterator:
TLgetprop_boolean_object_function:
TLgetprop_simple_object_iterator:
TLgetprop_function_fixnum:
TLgetprop_number_object_string_object:
TLgetprop_simple_object_number_object:
TLgetprop_boolean_object_boolean_object:
TLgetprop_function_function:
TLgetprop_function_iterator:
TLgetprop_boolean_object_array:
TLgetprop_function_regexp:
TLgetprop_boolean_object_string_object:
TLgetprop_string_object_string_object:
TLgetprop_function_special:
TLgetprop_regexp_fixnum:
TLgetprop_builtin_special:
TLgetprop_string_object_number_object:
TLgetprop_builtin_simple_object:
TLgetprop_builtin_builtin:
TLgetprop_simple_object_simple_object:
TLgetprop_number_object_boolean_object:
TLgetprop_boolean_object_iterator:
TLgetprop_function_number_object:
TLgetprop_string_object_iterator:
TLgetprop_number_object_iterator:
TLgetprop_string_object_boolean_object:
TLgetprop_simple_object_array:
TLgetprop_boolean_object_simple_object:
TLgetprop_regexp_iterator:
{ // normal_function normal_special vmgen.newsynth.DecisionDiagram$Leaf@69

#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag)
    printf("getprop: object (prop = %"PRIJSValue")\n", prop);
#endif /* DEBUG_GETPROP_VALUE */
#ifdef INLINE_CACHE
  dst = get_object_prop(context, obj, prop, NULL);
#else /* INLINE_CACHE */
  dst = get_object_prop(context, obj, prop);
#endif /* INLINE_CACHE */
#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag) {
    printf("getprop: ");
    print_value(context, dst, 1);
    printf("\n");
  }
#endif /* DEBUG_GETPROP_VALUE */
}
break;
case HTAGV_ARRAY:
Lgetprop5:
TLgetprop_array_fixnum:
{ // normal_array normal_fixnum vmgen.newsynth.DecisionDiagram$Leaf@235a

#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag)
    printf("getprop: array (prop = fixnum(%" PRIcint "))\n", fixnum_to_cint(prop));
#endif /* DEBUG_GETPROP_VALUE */
  dst = get_array_element(context, obj, fixnum_to_cint(prop));
#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag) {
    printf("getprop: ");
    print_value(context, dst, 1);
    printf("\n");
  }
#endif /* DEBUG_GETPROP_VALUE */
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@2609
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@e18
break;
case TV_FLONUM:
case TV_SPECIAL:
case TV_GENERIC:
Lgetprop6:
TLgetprop_any_function:
TLgetprop_any_number_object:
TLgetprop_any_array:
TLgetprop_any_flonum:
TLgetprop_any_iterator:
TLgetprop_any_string_object:
TLgetprop_any_regexp:
TLgetprop_any_special:
TLgetprop_any_boolean_object:
TLgetprop_any_builtin:
TLgetprop_any_simple_object:
switch((unsigned int) get_ptag(obj).v){ // vmgen.newsynth.DecisionDiagram$PTNode@169a(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
goto Lgetprop2;
break;
case TV_GENERIC:
Lgetprop7:switch((unsigned int) get_htag(obj).v){ // vmgen.newsynth.DecisionDiagram$HTNode@5a1(3)
case HTAGV_ITERATOR:
goto Lgetprop2;
break;
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_FUNCTION:
case HTAGV_BUILTIN:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
goto Lgetprop4;
break;
case HTAGV_ARRAY:
Lgetprop8:
TLgetprop_array_string:
TLgetprop_array_builtin:
TLgetprop_array_special:
TLgetprop_array_flonum:
TLgetprop_array_regexp:
TLgetprop_array_number_object:
TLgetprop_array_iterator:
TLgetprop_array_function:
TLgetprop_array_array:
TLgetprop_array_simple_object:
TLgetprop_array_boolean_object:
TLgetprop_array_string_object:
{ // normal_array normal_array vmgen.newsynth.DecisionDiagram$Leaf@185

#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag)
    printf("getprop: array (prop = %"PRIJSValue")\n", prop);
#endif /* DEBUG_GETPROP_VALUE */
  dst = get_array_prop(context, obj, prop);
#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag) {
    printf("getprop: ");
    print_value(context, dst, 1);
    printf("\n");
  }
#endif /* DEBUG_GETPROP_VALUE */
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@5a1
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@169a
break;
case TV_STRING:
case 3:
case 2:
case 1:
Lgetprop9:
TLgetprop_any_string:
switch((unsigned int) get_ptag(obj).v){ // vmgen.newsynth.DecisionDiagram$PTNode@1986(2)
case TV_STRING:
case 3:
case 2:
case 1:
case TV_FLONUM:
case TV_SPECIAL:
case TV_FIXNUM:
default:
goto Lgetprop2;
break;
case TV_GENERIC:
Lgetprop10:switch((unsigned int) get_htag(obj).v){ // vmgen.newsynth.DecisionDiagram$HTNode@6c2(3)
case HTAGV_ITERATOR:
goto Lgetprop2;
break;
case HTAGV_ARRAY:
goto Lgetprop8;
break;
case HTAGV_SIMPLE_OBJECT:
case 5:
case 4:
case 3:
case 2:
case 1:
case 0:
case HTAGV_FUNCTION:
case HTAGV_BUILTIN:
case HTAGV_REGEXP:
case HTAGV_BOXED_STRING:
case HTAGV_BOXED_NUMBER:
case HTAGV_BOXED_BOOLEAN:
default:
Lgetprop11:
TLgetprop_builtin_string:
TLgetprop_function_string:
TLgetprop_boolean_object_string:
TLgetprop_regexp_string:
TLgetprop_number_object_string:
TLgetprop_simple_object_string:
TLgetprop_string_object_string:
{ // normal_builtin normal_string vmgen.newsynth.DecisionDiagram$Leaf@52d

#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag)
    printf("getprop: object (prop = %s)\n", string_to_cstr(prop));
#endif /* DEBUG_GETPROP_VALUE */
#ifdef INLINE_CACHE
  {
    InlineCache *ic = &insns->inl_cache;
#ifdef IC_PROF
    ic->count++;
    ic_prof_count++;
    if (ic->pm == NULL)
      ic->unavailable++;
#endif /* IC_PROF */
    if (ic->pm == object_get_shape(obj)->pm && ic->prop_name == prop) {
      assert(ic->index <
             object_get_shape(obj)->n_embedded_slots +
             object_get_shape(obj)->n_extension_slots -
             (object_get_shape(obj)->n_extension_slots > 0 ? 1 : 0));
      dst = object_get_prop(obj, ic->index);
      if (dst == JS_EMPTY)
        dst = get_object_prop(context, obj, prop, ic);
#ifdef IC_PROF
      else {
        ic->hit++;
        ic_prof_hit++;
      }
#endif /* IC_PROF */
    } else {
      if (++ic->miss > INLINE_CACHE_RESET_THRESHOLD)
        ic->pm = NULL;
      dst = get_object_prop(context, obj, prop, ic);
    }
  }
#else /* INLINE_CACHE */
  dst = get_object_prop(context, obj, prop);
#endif /* INLINE_CACHE */
#ifdef DEBUG_GETPROP_VALUE
  if (trace_flag) {
    printf("getprop: ");
    print_value(context, dst, 1);
    printf("\n");
  }
#endif /* DEBUG_GETPROP_VALUE */
}
break;
}// vmgen.newsynth.DecisionDiagram$HTNode@6c2
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@1986
break;
} // vmgen.newsynth.DecisionDiagram$PTNode@550
#undef dst
#undef obj
#undef prop

