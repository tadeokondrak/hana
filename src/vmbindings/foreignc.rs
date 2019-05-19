use super::chmap::CHashMap;
use super::carray::CArray;
use super::function::Function;
use super::cnativeval::NativeValue;
use super::record::Record;
use super::gc::{malloc, drop};
use super::vm::Vm;
use super::env::Env;
use super::exframe::ExFrame;

#[allow(unused_attributes)]
pub mod foreignc {

use std::ffi::CStr;
use std::ptr::null;
use super::*;

// #region hmap
#[no_mangle]
pub unsafe extern "C" fn hmap_get(chm: *const CHashMap, ckey: *const libc::c_char) -> *const NativeValue {
    let key = String::from(CStr::from_ptr(ckey).to_str().unwrap());
    let hm = &*chm;
    if let Some(val) = hm.get(&key) {
        val
    } else {
        null()
    }
}

#[no_mangle]
pub unsafe extern "C" fn hmap_set(chm: *mut CHashMap, ckey: *const libc::c_char, val: NativeValue) {
    let key = String::from(CStr::from_ptr(ckey).to_str().unwrap());
    let hm = &mut *chm;
    hm.insert(key, val.clone());
}
// #endregion

// #region dict
#[no_mangle]
pub unsafe extern "C" fn dict_malloc() -> *mut Record {
    malloc(Record::new(), |ptr| drop::<Record>(ptr))
}

#[no_mangle]
pub unsafe extern "C" fn dict_get(cr: *const Record, ckey: *const libc::c_char) -> *const NativeValue {
    let key = String::from(CStr::from_ptr(ckey).to_str().unwrap());
    let r = &*cr;
    if let Some(val) = r.get(&key) {
        val
    } else {
        null()
    }
}
#[no_mangle]
pub unsafe extern "C" fn dict_get_str(cr: *const Record, ckey: *const String) -> *const NativeValue {
    let key = &*ckey;
    let r = &*cr;
    if let Some(val) = r.get(key) {
        val
    } else {
        null()
    }
}

#[no_mangle]
pub unsafe extern "C" fn dict_set(cr: *mut Record, ckey: *const libc::c_char, val: NativeValue) {
    let key = String::from(CStr::from_ptr(ckey).to_str().unwrap());
    let r = &mut *cr;
    r.insert(key, val.clone());
}
#[no_mangle]
pub unsafe extern "C" fn dict_set_str(cr: *mut Record, ckey: *const String, val: NativeValue) {
    let key = (&*ckey).clone();
    let r = &mut *cr;
    r.insert(key, val.clone());
}

// #endregion

// #region string
#[no_mangle]
pub unsafe extern "C" fn string_malloc(cstr: *mut libc::c_char) -> *mut String {
    let s = CStr::from_ptr(cstr).to_str().unwrap();
    malloc(String::from(s), |ptr| drop::<String>(ptr))
}

#[no_mangle]
pub unsafe extern "C" fn string_append(cleft: *const String, cright: *const String) -> *mut String {
    let left : &'static String = &*cleft;
    let right : &'static String = &*cright;
    let mut newleft = left.clone();
    newleft += right;
    malloc(newleft, |ptr| drop::<String>(ptr))
}

#[no_mangle]
pub unsafe extern "C" fn string_repeat(cleft: *const String, n : i64) -> *mut String {
    let left : &'static String = &*cleft;
    malloc(left.repeat(n as usize), |ptr| drop::<String>(ptr))
}

#[no_mangle]
pub unsafe extern "C" fn string_cmp(cleft: *const String, cright: *const String) -> i64 {
    let left : &'static String = &*cleft;
    let right : &'static String = &*cright;
    if left == right     {  0 }
    else if left < right { -1 }
    else                 {  1 }
}

#[no_mangle]
pub unsafe extern "C" fn string_at(cleft: *const String, idx : i64) -> *mut String {
    let left : &'static String = &*cleft;
    let to = idx as usize;
    malloc(left[to..=to].to_string(), |ptr| drop::<String>(ptr))
}

#[no_mangle]
pub unsafe extern "C" fn string_is_empty(s: *const String) -> bool {
    let left : &'static String = &*s;
    left.is_empty()
}
// #endregion

// #region function
#[no_mangle]
pub unsafe extern "C" fn function_malloc(addr: u32, nargs: u16, env: *const Env) -> *mut Function {
    malloc(Function::new(addr, nargs, env), |ptr| drop::<Function>(ptr))
}

#[no_mangle]
pub unsafe extern "C" fn function_set_bound_var(fun: *mut Function, slot: u16, val: NativeValue) {
    let fun = &mut *fun;
    fun.bound.set(slot, val)
}
// #endregion

// #region array
#[no_mangle]
pub unsafe extern "C" fn array_obj_malloc() -> *mut CArray<NativeValue> {
    malloc(CArray::new(), |ptr| {
        let array = &mut *(ptr as *mut CArray<NativeValue>);
        array.drop();
    })
}
#[no_mangle]
pub unsafe extern "C" fn array_obj_malloc_n(n: usize) -> *mut CArray<NativeValue> {
    malloc(CArray::reserve(n), |ptr| {
        let array = &mut *(ptr as *mut CArray<NativeValue>);
        array.drop();
    })
}
#[no_mangle]
pub unsafe extern "C" fn array_obj_repeat(carray: *const CArray<NativeValue>, n: usize) -> *mut CArray<NativeValue> {
    let array = &*carray;
    let mut result : CArray<NativeValue> = CArray::reserve(n);
    for i in 0..n {
        for j in 0..array.len() {
            result[j*array.len() + i] = array[j].clone();
        }
    }
    malloc(result, |ptr| {
        let array = &mut *(ptr as *mut CArray<NativeValue>);
        array.drop();
    })
}
// #endregion

// #region env
#[no_mangle]
pub unsafe extern "C" fn env_init(selfptr: *mut Env, nslots: u16, cvm: *mut Vm) {
    let env = &mut *selfptr;
    env.reserve(nslots);
    let vm = &mut *cvm;
    for i in 0..env.nargs {
        let val = vm.stack.top();
        env.set(i, val.clone());
        vm.stack.pop();
    }
}

#[no_mangle]
pub unsafe extern "C" fn env_free(selfptr: *mut Env) {
    std::ptr::drop_in_place(selfptr);
}

//
#[no_mangle]
pub unsafe extern "C" fn env_get(selfptr: *mut Env, slot: u16) -> NativeValue {
    let env = &mut *selfptr;
    env.get(slot)
}
#[no_mangle]
pub unsafe extern "C" fn env_get_up(selfptr: *mut Env, up: u16, slot: u16) -> NativeValue {
    let env = &mut *selfptr;
    env.get_up(up, slot)
}
#[no_mangle]
pub unsafe extern "C" fn env_set(selfptr: *mut Env, slot: u16, val: NativeValue) {
    let env = &mut *selfptr;
    env.set(slot, val);
}

//
#[no_mangle]
pub unsafe extern "C" fn vm_enter_env(selfptr: *mut Vm, fun: *mut Function) -> *const Env {
    let vm = &mut *selfptr;
    let fun = &mut *fun;
    vm.enter_env(fun);
    vm.localenv
}
#[no_mangle]
pub unsafe extern "C" fn vm_enter_env_tail(selfptr: *mut Vm, fun: *mut Function) -> *const Env {
    let vm = &mut *selfptr;
    let fun = &mut *fun;
    vm.enter_env_tail(fun);
    vm.localenv
}

#[no_mangle]
pub unsafe extern "C" fn vm_leave_env(selfptr: *mut Vm) -> bool {
    let vm = &mut *selfptr;
    if (&*vm.localenv).retip == std::u32::MAX {
        return true;
    }
    vm.leave_env();
    false
}
// #endregion

// #region exceptions
#[no_mangle]
pub unsafe extern "C" fn exframe_set_handler(selfptr: *mut ExFrame, proto: *const Record, fun: *const Function) {
    let exframe = &mut *selfptr;
    exframe.set_handler(proto, (*fun).clone());
}
#[no_mangle]
pub unsafe extern "C" fn exframe_native_stack_depth(selfptr: *const ExFrame) -> usize {
    let exframe = &*selfptr;
    exframe.unwind_native_call_depth
}

#[no_mangle]
pub unsafe extern "C" fn vm_enter_exframe(cvm: *mut Vm) -> *mut ExFrame {
    let vm = &mut *cvm;
    vm.enter_exframe()
}

#[no_mangle]
pub unsafe extern "C" fn vm_leave_exframe(cvm: *mut Vm) {
    let vm = &mut *cvm;
    vm.leave_exframe()
}

#[no_mangle]
pub unsafe extern "C" fn vm_raise(cvm: *mut Vm) -> bool {
    let vm = &mut *cvm;
    vm.raise()
}
// #endregion

// #region modules
#[no_mangle]
pub unsafe extern "C" fn vm_load_module(cvm: *mut Vm, cpath: *const libc::c_char) {
    let path = CStr::from_ptr(cpath).to_str().unwrap();
    let vm = &mut *cvm;
    vm.load_module(&path);
}
// #endregion

// #region value
#[no_mangle]
pub extern "C" fn value_print(val: NativeValue) {
    if (val.r#type as u8) < 127 {
        eprint!("{:?}", val.unwrap());
    }
}
// #endregion


}