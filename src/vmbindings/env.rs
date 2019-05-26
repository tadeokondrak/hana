//! Provides the stack frame for the virtual machine

use super::cnativeval::NativeValue;
use super::vm::Value;

#[derive(Clone)]
/// Stack frame for the virtual machine
pub struct Env {
    /// Slot indexes access SHOULD be bounded
    /// whenever the script is compiled to bytecode
    pub slots: Vec<NativeValue>,

    /// Cached number of args the function was called with
    pub nargs : u16,

    /// Lexical parent of the current environment
    ///
    /// This is used for getting values on the previous stack frame.
    pub lexical_parent : *const Env,

    /// Instruction pointer to return to on OP_RET
    pub retip : u32,
}

impl Env {

    pub fn new(retip: u32,
              lexical_parent: *const Env,
              nargs: u16) -> Env {
        Env {
            slots: Vec::new(),
            nargs: nargs,
            lexical_parent: lexical_parent,
            retip: retip
        }
    }

    pub fn copy(other : &Env) -> Env {
        Env {
            slots: other.slots.clone(),
            nargs: 0,
            lexical_parent: other.lexical_parent,
            retip: std::u32::MAX
        }
    }

    pub unsafe fn get(&self, idx: u16) -> NativeValue {
        self.slots.get_unchecked(idx as usize).clone()
    }
    pub unsafe fn get_up(&self, up: u16, idx: u16) -> NativeValue {
        let mut env = self.lexical_parent;
        for _ in 1..up {
            env = (*env).lexical_parent;
        }
        (*env).get(idx)
    }

    pub unsafe fn set(&mut self, idx: u16, val: NativeValue) {
        let elem = self.slots.get_unchecked_mut(idx as usize);
        *elem = val;
    }

    pub fn reserve(&mut self, nslots: u16) {
        self.slots.resize(nslots as usize, Value::Nil.wrap());
    }

}