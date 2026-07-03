#[allow(unused, non_camel_case_types, non_upper_case_globals, non_snake_case)]
mod bindgen;

use bindgen::{GST_TYPE_FIND_LIKELY, GST_TYPE_FIND_MAXIMUM};

mod helpers;
use helpers::{suggest, TypeFind};

#[no_mangle]
pub extern "C" fn musepack_type_find(tf: &mut TypeFind, _unused: *mut std::os::raw::c_void) {
    let Some(data) = tf.peek::<4>(0) else {
        return;
    };

    let mut streamversion = None;
    let mut prob = None;
    if data.starts_with(b"MP+") {
        prob = if data[3] & 0x7f == 7 {
            Some(GST_TYPE_FIND_MAXIMUM)
        } else {
            Some(GST_TYPE_FIND_LIKELY + 10)
        };
        streamversion = Some(7);
    } else if data.starts_with(b"MPCK") {
        streamversion = Some(8);
        prob = Some(GST_TYPE_FIND_MAXIMUM);
    }

    if let Some((streamversion, prob)) = Option::zip(streamversion, prob) {
        suggest!(tf,
            prob,
            c"audio/x-musepack",
            [
                (c"streamversion", i32: streamversion),
            ],
        );
    }
}
