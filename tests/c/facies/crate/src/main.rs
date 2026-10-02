// tests/c/facies/crate/src/main.rs -- the host.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Exsecutor authors.
//
// The face is the generated `extern "C"` block, included verbatim; the
// consumer is written against the signatures it expects, so a face
// regenerated from a changed signature stops it compiling. Warnings are
// errors, so a declaration rustc warns about (an improper C type, say) fails
// the build. An UNUSED foreign declaration draws no warning (measured with a
// consumer that calls nothing), so a host need not call the whole face.
#![deny(warnings)]

include!(concat!(env!("OUT_DIR"), "/face.rs"));
include!(concat!(env!("OUT_DIR"), "/consumer.rs"));

/// The one symbol the unit imports (docs/design/c-backend.md D1). Reached
/// only by a trap; it must not return.
#[no_mangle]
pub extern "C" fn exsrt_abortus(kind: u32) -> ! {
    eprintln!("exsecutor: abortus {kind}");
    std::process::exit(70)
}

fn main() {
    std::process::exit(consume());
}
