//! Provides the native value representation
//! used by the virtual machine

use super::function::Function;
use super::gc::{ref_dec, ref_inc, Gc, GcManager, GcTraceable};
use super::record::Record;
use super::value::{NativeFnData, Value};

#[repr(u8)]
#[allow(non_camel_case_types, dead_code)]
#[derive(Debug, PartialEq, Clone, Copy)]
/// Type of the native value
pub enum NativeValueType {
    TYPE_NIL = 0,
    TYPE_INT = 1,
    TYPE_FLOAT = 2,
    TYPE_NATIVE_FN = 3,
    TYPE_FN = 4,
    TYPE_STR = 5,
    TYPE_DICT = 6,
    TYPE_ARRAY = 7,
    TYPE_INTERPRETER_ERROR = 127,
}

#[repr(C, packed)]
#[derive(Debug, PartialEq, Clone, Copy)]
/// Native value representation used by the virtual machine
pub struct NativeValue {
    pub data: u64,
    pub r#type: NativeValueType,
}

impl NativeValue {
    /// Converts the native value into a wrapped Value.
    pub fn unwrap(&self) -> Value {
        use std::mem::transmute;
        #[allow(non_camel_case_types)]
        match &self.r#type {
            NativeValueType::TYPE_NIL => Value::Nil,
            NativeValueType::TYPE_INT => unsafe { Value::Int(transmute::<u64, i64>(self.data)) },
            NativeValueType::TYPE_FLOAT => Value::Float(f64::from_bits(self.data)),
            NativeValueType::TYPE_NATIVE_FN => unsafe {
                Value::NativeFn(transmute::<u64, NativeFnData>(self.data))
            },
            NativeValueType::TYPE_FN => unsafe {
                Value::Fn(Gc::from_raw(self.data as *mut Function))
            },
            NativeValueType::TYPE_STR => unsafe {
                Value::Str(Gc::from_raw(self.data as *mut String))
            },
            NativeValueType::TYPE_DICT => unsafe {
                Value::Record(Gc::from_raw(self.data as *mut Record))
            },
            NativeValueType::TYPE_ARRAY => unsafe {
                Value::Array(Gc::from_raw(self.data as *mut Vec<NativeValue>))
            },
            _ => {
                panic!("type was: {:?}", self.r#type)
            }
        }
    }

    pub fn as_pointer(&self) -> Option<*mut libc::c_void> {
        #[allow(non_camel_case_types)]
        match self.r#type {
            NativeValueType::TYPE_FN
            | NativeValueType::TYPE_STR
            | NativeValueType::TYPE_DICT
            | NativeValueType::TYPE_ARRAY => {
                if self.data == 0 { None }
                else { Some(self.data as *mut libc::c_void) }
            },
            _ => None
        }
    }

    // reference counting
    pub unsafe fn ref_inc(&self) {
        #[allow(non_camel_case_types)]
        match self.r#type {
            NativeValueType::TYPE_FN
            | NativeValueType::TYPE_STR
            | NativeValueType::TYPE_DICT
            | NativeValueType::TYPE_ARRAY => {
                ref_inc(self.data as *mut libc::c_void);
            }
            _ => {}
        }
    }

    pub unsafe fn ref_dec(&self) {
        #[allow(non_camel_case_types)]
        match self.r#type {
            NativeValueType::TYPE_FN
            | NativeValueType::TYPE_STR
            | NativeValueType::TYPE_DICT
            | NativeValueType::TYPE_ARRAY => {
                ref_dec(self.data as *mut libc::c_void);
            }
            _ => {}
        }
    }
}
