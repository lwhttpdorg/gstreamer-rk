use crate::bindgen;

#[repr(transparent)]
pub struct TypeFind(pub(crate) bindgen::GstTypeFind);

impl TypeFind {
    pub fn peek_var(&mut self, offset: i64, size: u32) -> Option<&[u8]> {
        assert!(size > 0);

        unsafe {
            let data = bindgen::gst_type_find_peek(&mut self.0, offset, size);
            if data.is_null() {
                None
            } else {
                Some(std::slice::from_raw_parts(data, size as usize))
            }
        }
    }

    pub fn peek<const S: usize>(&mut self, offset: i64) -> Option<&[u8; S]> {
        assert!(S <= u32::MAX as usize);
        assert!(S > 0);

        unsafe {
            let data = bindgen::gst_type_find_peek(&mut self.0, offset, S as u32);
            if data.is_null() {
                None
            } else {
                Some(&*(data as *const [u8; S]))
            }
        }
    }

    pub fn length(&mut self) -> u64 {
        unsafe { bindgen::gst_type_find_get_length(&mut self.0) }
    }
}

/// Allows to enforce a specific type for the argument.
///
/// Helper function for the macros below.
pub(crate) fn type_check<T: ?Sized>(_v: &T) {}

/// Maps a type name literal to a `GType` id.
macro_rules! map_type_name {
    (i32) => {
        // bindgen can't handle this
        6 << bindgen::G_TYPE_FUNDAMENTAL_SHIFT
    };
    (str) => {
        // bindgen can't handle this
        16 << bindgen::G_TYPE_FUNDAMENTAL_SHIFT
    };
    (bool) => {
        // bindgen can't handle this
        5 << bindgen::G_TYPE_FUNDAMENTAL_SHIFT
    };
}

/// Maps a type name literal and value to the value used in the call.
macro_rules! map_value {
    (i32, $val:expr) => {{
        $crate::helpers::type_check::<i32>(&$val);
        $val
    }};
    (str, $val:expr) => {{
        $crate::helpers::type_check::<std::ffi::CStr>(&$val);
        $val
    }
    .as_ptr()};
    (bool, $val:expr) => {{
        $crate::helpers::type_check::<bool>(&$val);
        if $val {
            1i32
        } else {
            0i32
        }
    }};
}

/// Macro to call `gst_type_find_suggest_simple()` safely.
macro_rules! suggest {
    ($tf:expr, $prob:expr, $name:expr, []) => {
        unsafe {
            bindgen::gst_type_find_suggest_simple(
                &mut $tf.0,
                $prob,
                {
                    $crate::helpers::type_check::<std::ffi::CStr>(&$name);
                    $name
                }.as_ptr(),
                std::ptr::null::<()>(),
            );
        }
    };
    ($tf:expr, $prob:expr, $name:expr, [ $( ($key:expr, $type:ident : $val:expr) , )*] $(,)?) => {
        unsafe {
            bindgen::gst_type_find_suggest_simple(
                &mut $tf.0,
                $prob,
                {
                    $crate::helpers::type_check::<std::ffi::CStr>(&$name);
                    $name
                }.as_ptr(),
                $(
                    {
                        $crate::helpers::type_check::<std::ffi::CStr>(&$key);
                        $key
                    }.as_ptr(),
                    $crate::helpers::map_type_name!($type),
                    $crate::helpers::map_value!($type, $val),
                )*
                std::ptr::null::<()>(),
            );
        }
    };
}

pub(crate) use {map_type_name, map_value, suggest};
