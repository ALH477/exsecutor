// examples/abortus/proba.rs -- the same guard, with a Rust host on top: the
// shape of Punctim's dcf-ws-bridge (custos) and Oligarchy's reliquary (arca).
//
// Rust calls the C wrappers in exempla.c; each wrapper calls
// exs_tutela_curre, which holds the setjmp; the trap's longjmp lands there.
// So the jump crosses only C frames -- the thunk, the emitted Exsecutor
// functions, exsrt_abortus -- and never a Rust one, which is what makes it
// defined from Rust's side: no Rust frame is skipped, nothing of Rust's is
// unwound, and the wrapper returns to Rust by an ordinary return.
//
// No Cargo, no crates: proba_c.sh links this against a static library of
// the three units, tutela.c and exempla.c with plain rustc.

use std::thread;

extern "C" {
    fn custos_admitte_tutum(d: *mut u8, n: u64, r: *mut u64) -> u32;
    fn custos_redundantia_sarcinae_tutum(d: *mut u8, n: u64, r: *mut u64) -> u32;
    fn arca_saltus_capitis_tutum(h: *mut u8, r: *mut u64) -> u32;
    fn arca_saltus_tutum(m: u64, r: *mut u64) -> u32;
    fn probatio_circuitus_tutum(n: u64, r: *mut u64) -> u32;
    fn exs_tutela_profunditas() -> u32;
}

/// Ok(verdict) or Err(trap kind): the host's view of a returning trap.
fn admitte(d: &mut [u8; 32], n: u64) -> Result<u64, u32> {
    let mut r = 0u64;
    match unsafe { custos_admitte_tutum(d.as_mut_ptr(), n, &mut r) } {
        0 => Ok(r),
        k => Err(k),
    }
}

fn saltus_capitis(h: &mut [u8; 512]) -> Result<u64, u32> {
    let mut r = 0u64;
    match unsafe { arca_saltus_capitis_tutum(h.as_mut_ptr(), &mut r) } {
        0 => Ok(r),
        k => Err(k),
    }
}

fn frame(hex: &str) -> [u8; 32] {
    let mut d = [0xa5u8; 32];
    for i in 0..hex.len() / 2 {
        d[i] = u8::from_str_radix(&hex[2 * i..2 * i + 2], 16).unwrap();
    }
    d
}

fn main() {
    let mut passed = 0u32;
    let mut total = 0u32;
    let mut check = |ok: bool, what: &str| {
        total += 1;
        if ok {
            passed += 1;
        }
        println!("  [{}] {}", if ok { "ok" } else { "FAIL" }, what);
    };

    let mut good = frame("d310000000000000000000000000005b80");
    let mut bad = frame("d310000000000000000000000000005b81");
    check(admitte(&mut good, 17) == Ok(0), "rust: custos admits a filler frame");
    check(admitte(&mut bad, 17) == Ok(3), "rust: custos refuses a bad CRC with verdict 3");

    let mut garbage = [b'x'; 512];
    check(
        saltus_capitis(&mut garbage) == Err(1),
        "rust: saltus(magnitudo(garbage header)) is Err(1), not an abort",
    );
    check(unsafe { exs_tutela_profunditas() } == 0, "rust: guard chain empty after the trap");
    check(admitte(&mut good, 17) == Ok(0), "rust: custos still admits after the trap");

    let mut win = [0u8; 32];
    let mut r = 0u64;
    let k = unsafe { custos_redundantia_sarcinae_tutum(win.as_mut_ptr(), 33, &mut r) };
    check(k == 1, "rust: custos CRC window past its end is Err(1)");

    // Threads: each one traps and recovers on its own guard chain.
    let fila: Vec<_> = (0..4u64)
        .map(|id| {
            thread::spawn(move || {
                let mut errata = 0u32;
                for i in 0..50_000u64 {
                    let mut r = 0u64;
                    let m = i * 4 + id;
                    if unsafe { arca_saltus_tutum(m, &mut r) } != 0 || r != (m + 511) / 512 * 512 {
                        errata += 1;
                    }
                    if unsafe { arca_saltus_tutum(u64::MAX - (m & 255), &mut r) } != 1 {
                        errata += 1;
                    }
                    if unsafe { probatio_circuitus_tutum(9, &mut r) } != 5 {
                        errata += 1;
                    }
                }
                errata
            })
        })
        .collect();
    let errata: u32 = fila.into_iter().map(|h| h.join().unwrap()).sum();
    check(errata == 0, "rust: 4 threads x 50,000 rounds of (good, overflow trap, terminus trap)");

    println!("abortus-rs: {}/{}", passed, total);
    std::process::exit(if passed == total { 0 } else { 1 });
}
