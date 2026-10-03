// tests/c/facies/crate/build.rs -- builds the unit into a static library.
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DeMoD LLC.
//
// Compiles the unit named by EXS_FACIES_UNIT with $CC (default cc) into a
// static library and links it, and copies the generated face (EXS_FACIES_RS)
// and the consumer (EXS_FACIES_USE, which defines `fn consume() -> i32`)
// into OUT_DIR for src/main.rs to include!.
// No `cc` crate: it would be the one dependency, and this must build offline.
use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn need(var: &str) -> String {
    println!("cargo:rerun-if-env-changed={var}");
    env::var(var).unwrap_or_else(|_| panic!("facies: {var} is not set"))
}

fn main() {
    let unit = need("EXS_FACIES_UNIT");
    let face = need("EXS_FACIES_RS");
    let used = need("EXS_FACIES_USE");
    println!("cargo:rerun-if-env-changed=CC");
    let cc = env::var("CC").unwrap_or_else(|_| "cc".to_string());
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let obj = out.join("unit.o");
    let st = Command::new(&cc)
        .args(["-std=c11", "-O2", "-fPIC", "-fno-fast-math", "-Wall", "-Wextra",
               "-Werror", "-Wno-unused-function", "-c", &unit, "-o"])
        .arg(&obj)
        .status()
        .expect("facies: cannot run the C compiler");
    assert!(st.success(), "facies: {cc} failed on {unit}");
    let lib = out.join("libexsunit.a");
    let _ = fs::remove_file(&lib);
    let st = Command::new("ar").arg("rcs").arg(&lib).arg(&obj).status()
        .expect("facies: cannot run ar");
    assert!(st.success(), "facies: ar failed");
    fs::copy(&face, out.join("face.rs")).expect("facies: copy face");
    fs::copy(&used, out.join("consumer.rs")).expect("facies: copy consumer");
    println!("cargo:rerun-if-changed={unit}");
    println!("cargo:rerun-if-changed={face}");
    println!("cargo:rerun-if-changed={used}");
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=exsunit");
}
