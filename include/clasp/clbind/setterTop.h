template <typename SetterPolicies, typename OT, typename VariablePtrType>
class WRAPPER_Setter : public core::SimpleFun_O {
public:
  typedef WRAPPER_Setter<SetterPolicies, OT, VariablePtrType> MyType;
  typedef core::SimpleFun_O TemplatedBase;
  typedef typename memberpointertraits<VariablePtrType>::member_type MemberType;
  typedef clbind::Wrapper<MemberType, MemberType*> WrapperType;

public:
  VariablePtrType mptr;

public:
  WRAPPER_Setter(VariablePtrType ptr, core::FunctionDescription_sp fdesc, core::T_sp code)
    : mptr(ptr), SimpleFun_O(fdesc, code, core::XepStereotype<MyType>()){};

  virtual size_t templatedSizeof() const { return sizeof(*this); };

  void fixupInternalsForSnapshotSaveLoad(snapshotSaveLoad::Fixup* fixup) {
    this->TemplatedBase::fixupInternalsForSnapshotSaveLoad(fixup);
    printf("%s:%d:%s What do we do with mptr %p\n", __FILE__, __LINE__, __FUNCTION__, *(void**)&this->mptr);
    // this->fixupOneCodePointer( fixup, (void**)&this->mptr );
  };

  static inline LCC_RETURN LISP_CALLING_CONVENTION() {
    MyType* closure = gctools::untag_general<MyType*>((MyType*)lcc_closure);
    INCREMENT_FUNCTION_CALL_COUNTER(closure);
    DO_DRAG_CXX_CALLS();
    ASSERT(lcc_nargs == 2);
    core::T_sp arg0((gctools::Tagged)lcc_args[0]);
    core::T_sp arg1((gctools::Tagged)lcc_args[1]);
    OT* objPtr = gc::As<core::WrappedPointer_sp>(arg1)->cast<OT>();
    translate::from_object<MemberType> fvalue(arg0);
    (*objPtr).*(closure->mptr) = fvalue._v;
    gctools::return_type retv(arg0.raw_(), 1);
    return retv;
  }
  // FIXME: make T_O*, T_O* direct and the rest errors.
  template <typename... Ts>
  static inline LCC_RETURN entry_point_fixed(core::T_O* lcc_closure, Ts... args) {
    core::T_O* lcc_args[sizeof...(Ts)] = {args...};
    return entry_point_n(lcc_closure, sizeof...(Ts), lcc_args);
  }
};
