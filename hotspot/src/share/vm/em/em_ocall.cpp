

#ifndef ENCLAVE_UNIX

#include <prims/nativeLookup.hpp>
#include <interpreter/oopMapCache.hpp>
#include "em_ocall.h"
#include "interpreter/interpreterRuntime.hpp"
#include "CompilerEnclave.h"
#include "PreInterpreterCallStubGenerator.h"

#define CACHE_ENTRY(method, bcp) method->constants()->cache()->entry_at(Bytes::get_native_u2((address)bcp + 1))
#define CODE_AT(method, bcp) Bytecodes::code_at(method, bcp)
#define IS_RESOLVE(method, bcp) CACHE_ENTRY(method, bcp)->is_resolved(CODE_AT(method, (address)bcp))

#ifdef DB_OCALL
#define OCALL_TRACE printf("OCALL %s\n", __FUNCTION__)
#else
#define OCALL_TRACE
#endif

int resolve_count = 0;

long long int total_time = 0;

void* ocall_interpreter(void* r14, int size, void* method, void* thread, void* sender) {
    OCALL_TRACE;
    return PreInterpreterCallStubGenerator::ocall_interpreter_stub(r14, size, method, thread, sender);
}

void* ocall_jvm_malloc(int size) {
    return malloc(size);
}

void ocall_jvm_resolve_get_put(int byte, void* mh, int idx, void* bcp) {
    resolve_count += 1;
    timeval tvs, tve;
    gettimeofday(&tvs, 0);
    OCALL_TRACE;
//    printf("ocall resolve %d, method %lx, %d, %lx\n", byte,(intptr_t) mh, idx, (intptr_t)bcp);
    // printf("ocall resolve method name %s\n", ((Method*)mh)->name()->as_C_string());
    Method* method = (Method*)mh;
    JavaThread* thread = JavaThread::current();
    Bytecodes::Code bytecode = (Bytecodes::Code)byte;
    // resolve field
    fieldDescriptor info;
    constantPoolHandle pool(thread, method->constants());
    bool is_put    = (bytecode == Bytecodes::_putfield  || bytecode == Bytecodes::_putstatic);
    bool is_static = (bytecode == Bytecodes::_getstatic || bytecode == Bytecodes::_putstatic);

    {
        LinkResolver::resolve_field_access(info, pool, idx,
                                           bytecode, thread);
    } // end JvmtiHideSingleStepping

    // check if link resolution caused cpCache to be updated
    if (IS_RESOLVE(method, bcp)) {
        gettimeofday(&tve, 0);
        total_time += ((tve.tv_sec - tvs.tv_sec) * 1000000 + (tve.tv_usec - tvs.tv_usec));
        printf("resolve %d %lld\n", resolve_count, total_time);
        return;
    }

    // compute auxiliary field attributes
    TosState state  = as_TosState(info.field_type());

    // We need to delay resolving put instructions on final fields
    // until we actually invoke one. This is required so we throw
    // exceptions at the correct place. If we do not resolve completely
    // in the current pass, leaving the put_code set to zero will
    // cause the next put instruction to reresolve.
    Bytecodes::Code put_code = (Bytecodes::Code)0;

    // We also need to delay resolving getstatic instructions until the
    // class is intitialized.  This is required so that access to the static
    // field will call the initialization function every time until the class
    // is completely initialized ala. in 2.17.5 in JVM Specification.
    InstanceKlass* klass = InstanceKlass::cast(info.field_holder());
    bool uninitialized_static = ((bytecode == Bytecodes::_getstatic || bytecode == Bytecodes::_putstatic) &&
                                 !klass->is_initialized());
    Bytecodes::Code get_code = (Bytecodes::Code)0;

    if (!uninitialized_static) {
        get_code = ((is_static) ? Bytecodes::_getstatic : Bytecodes::_getfield);
        if (is_put || !info.access_flags().is_final()) {
            put_code = ((is_static) ? Bytecodes::_putstatic : Bytecodes::_putfield);
        }
    }
    CACHE_ENTRY(method, bcp)->set_field(
            get_code,
            put_code,
            info.field_holder(),
            info.index(),
            info.offset(),
            state,
            info.access_flags().is_final(),
            info.access_flags().is_volatile(),
            pool->pool_holder()
    );
    gettimeofday(&tve, 0);
    total_time += ((tve.tv_sec - tvs.tv_sec) * 1000000 + (tve.tv_usec - tvs.tv_usec));
    printf("resolve %d %lld\n", resolve_count, total_time);
}

void ocall_jvm_resolve_invoke_handle() {
    OCALL_TRACE;
    InterpreterRuntime::resolve_invokehandle(JavaThread::current());
}

void ocall_jvm_resolve_invoke_dynamic() {
    OCALL_TRACE;
    InterpreterRuntime::resolve_invokehandle(JavaThread::current());
}

void ocall_jvm_resolve_invoke(int byte, void* mh, int bci, void* recv, int idx, void* bcp, void* recv_klass) {
    resolve_count += 1;
    timeval tvs, tve;
    gettimeofday(&tvs, 0);
    OCALL_TRACE;
//  InterpreterRuntime::resolve_invoke(JavaThread::current(), (Bytecodes::Code)bytecode);
//     printf("ocall resolve invoke %d, method %lx, idx %d, bcp %lx, bci %d, recv_klass %lx\n", byte,(intptr_t) mh, idx, (intptr_t)bcp, bci, (intptr_t)recv_klass);
//    printf("ocall resolve %d method %s; class %s; recv %lx;\n", byte, ((Method*)mh)->name()->as_C_string(), ((Method*)mh)->klass_name()->as_C_string(), (intptr_t)recv_klass);
    JavaThread *thread = JavaThread::current();
    Method* method = (Method*)mh;
    Handle receiver(thread, NULL);
    Bytecodes::Code bytecode = (Bytecodes::Code)byte;
    if (bytecode == Bytecodes::_invokevirtual || bytecode == Bytecodes::_invokeinterface ||
        bytecode == Bytecodes::_invokespecial) {
        ResourceMark rm(thread);
        methodHandle m (thread, method);
        Bytecode_invoke call(m, bci);
        Symbol* signature = call.signature();
        receiver = Handle(thread, (oop)recv);
        assert(Universe::heap()->is_in_reserved_or_null(receiver()),
               "sanity check");
        assert(receiver.is_null() ||
               !Universe::heap()->is_in_reserved(receiver->klass()),
               "sanity check");
    }

    // resolve method
    CallInfo info;
    constantPoolHandle pool(thread, method->constants());

    {
        LinkResolver::resolve_invoke(info, receiver, pool,
                                     idx, bytecode, thread, (Klass*)recv_klass);
    }

    // check if link resolution caused cpCache to be updated
    if (IS_RESOLVE(method, bcp)) {
        gettimeofday(&tve, 0);
        total_time += ((tve.tv_sec - tvs.tv_sec) * 1000000 + (tve.tv_usec - tvs.tv_usec));
        printf("resolve %d %lld\n", resolve_count, total_time);
        return;
    }


    // Get sender or sender's host_klass, and only set cpCache entry to resolved if
    // it is not an interface.  The receiver for invokespecial calls within interface
    // methods must be checked for every call.
    InstanceKlass* sender = pool->pool_holder();
    sender = sender->is_anonymous() ? InstanceKlass::cast(sender->host_klass()) : sender;

    switch (info.call_kind()) {
        case CallInfo::direct_call:
            CACHE_ENTRY(method, bcp)->set_direct_call(
                    bytecode,
                    info.resolved_method(),
                    sender->is_interface());
            break;
        case CallInfo::vtable_call:
            CACHE_ENTRY(method, bcp)->set_vtable_call(
                    bytecode,
                    info.resolved_method(),
                    info.vtable_index());
            break;
        case CallInfo::itable_call:
            CACHE_ENTRY(method, bcp)->set_itable_call(
                    bytecode,
                    info.resolved_method(),
                    info.itable_index());
            break;
        default:  ShouldNotReachHere();
    }
    gettimeofday(&tve, 0);
    total_time += ((tve.tv_sec - tvs.tv_sec) * 1000000 + (tve.tv_usec - tvs.tv_usec));
    printf("resolve %d %lld\n", resolve_count, total_time);
}

void* ocall_jvm_resolve_ldc(void*p, int index, int byte) {
    OCALL_TRACE;
//    printf("ocall resolve ldc %lx %d\n", (intptr_t)p,index);
    JavaThread *thread = JavaThread::current();
    ResourceMark rm(thread);
    ConstantPool* pool = (ConstantPool*)p;
    Bytecodes::Code bytecode = (Bytecodes::Code)byte;
    if (bytecode > Bytecodes::number_of_java_codes) {
        return pool->resolve_cached_constant_at(index, thread);
    } else {
        return pool->resolve_constant_at(index, thread);
    }
}

void* ocall_jvm_ldc(bool wide, void* p, int index) {
    OCALL_TRACE;
    JavaThread *thread = JavaThread::current();
    ConstantPool* pool = (ConstantPool*)p;
    constantTag tag = pool->tag_at(index);

    assert (tag.is_unresolved_klass() || tag.is_klass(), "wrong ldc call");
    Klass* klass = pool->klass_at(index, thread);
    oop java_class = klass->java_mirror();
    return java_class;
}

void* ocall_klass_type_array() {
    OCALL_TRACE;
    return (void*)Universe::typeArrayKlassObj();
}

void* ocall_klass_type() {
    OCALL_TRACE;
    return (void*)SystemDictionary::_box_klasses;
}

void* ocall_klass_get(void* p, int index) {
    resolve_count += 1;
    timeval tvs, tve;
    gettimeofday(&tvs, 0);
    OCALL_TRACE;
//    printf("ocall klass_get %lx %d\n", (intptr_t)p, index);
    int idx = (index < 0)? -index : index;
    ConstantPool *pool = (ConstantPool*)p;
    Klass* k_oop = pool->klass_at(idx, JavaThread::current());

    // if it is a quick cc
    if (index < 0)
        return k_oop;
    instanceKlassHandle klass (JavaThread::current(), k_oop);

    // Make sure we are not instantiating an abstract klass
    klass->check_valid_for_instantiation(true, JavaThread::current());

    // Make sure klass is initialized
    klass->initialize(JavaThread::current());
    gettimeofday(&tve, 0);
    total_time += ((tve.tv_sec - tvs.tv_sec) * 1000000 + (tve.tv_usec - tvs.tv_usec));
    printf("resolve %d %lld\n", resolve_count, total_time);
    return k_oop;
}

void* ocall_obj_array_klass_get(void*p, int index) {
    OCALL_TRACE;
//    printf("ocall array_klass_get %lx %d\n", (intptr_t)p, index);
    JavaThread *thread = JavaThread::current();
    ConstantPool *pool = (ConstantPool*)p;
    Klass* klass = pool->klass_at(index, thread);
    if (klass->oop_is_array()) {
        Klass* k = ((ArrayKlass*)klass)->array_klass(1 + ((ArrayKlass*)klass)->dimension(), thread);
        return ArrayKlass::cast(k);
    } else {
        return ((ArrayKlass*)klass)->array_klass(1, thread);
    }
}

void* ocall_multi_array_klass_get(void* p, int index) {
    OCALL_TRACE;
//    printf("ocall multi array_klass_get %lx %d\n", (intptr_t)p, index);
    ConstantPool *pool = (ConstantPool*)p;
    JavaThread* thread = JavaThread::current();
    Klass *klass = pool->klass_at(index, thread);
    ArrayKlass *ak = ArrayKlass::cast(klass);
//    for (int i = 1;i < ak->dimension();i++) {
//        ak->array_klass(i, thread);
//    }
    return klass;
}

void ocall_jvm_pre_native(void* method, int resolve) {
    OCALL_TRACE;
    if (!resolve)
        InterpreterRuntime::prepare_native_call(JavaThread::current(), (Method*)method);
}

void* ocall_array_klass(void* klass_v, int rank, int get_all) {
    OCALL_TRACE;
    Klass* klass = (Klass*)klass_v;
    return klass->array_klass(rank, JavaThread::current());
}

void* ocall_klass_resolve_or_fail(const char* name) {
  OCALL_TRACE;
  JavaThread* thread = JavaThread::current();
  TempNewSymbol s = SymbolTable::new_symbol(name, thread);
  return SystemDictionary::resolve_or_fail(s, Handle(thread, NULL),
                            Handle(thread, NULL), true, thread);
}

void ocall_klass_compute_oopmap(void* k, void* m, int bci) {
    OCALL_TRACE;
//    printf("oopmap %lx %lx %d\n", (intptr_t)k, (intptr_t)m, bci);
    InstanceKlass* klass = (InstanceKlass*)k;
    Method* method = (Method*)m;
    InterpreterOopMap map;
    klass->mask_for(methodHandle(method), bci, &map);
}

#endif
