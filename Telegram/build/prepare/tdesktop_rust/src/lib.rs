//! One static library with tlottie and twidget, so Telegram Desktop links a single copy of the
//! Rust standard library and one Rust personality routine. Both C APIs are exported unchanged.

extern crate tlottie;
extern crate twidget;
