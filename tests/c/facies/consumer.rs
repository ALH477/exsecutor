// tests/c/facies/consumer.rs -- facies.exsc's face, called.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeMoD LLC.
//
// Calls every function of the face once, with arguments whose answers are known: 0 when all agree, else the
// number of the first that does not.
fn consume() -> i32 {
    let mut b = [9u8, 8, 7, 6];
    unsafe {
        if exs_summa(2, 3) != 6 { return 1; }
        if exs_prima(b.as_mut_ptr()) != 9 { return 2; }
        if exs_dimidium(5.0) != 2.5 { return 3; }
        if exs_dimidium_f(3.0) != 1.5 { return 4; }
        exs_quiesce();
    }
    0
}
