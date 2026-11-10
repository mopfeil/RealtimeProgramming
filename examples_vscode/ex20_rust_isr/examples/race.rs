//! Rejected on purpose: sharing a plain variable between main and an ISR.
//!
//!     cargo build --release --example race
//!
//! The compiler stops with
//!   error[E0277]: `RefCell<Sample>` cannot be shared between threads safely
//! An interrupt handler is another thread of execution: a `static` must be
//! `Sync`, and `RefCell` is not, because its borrow counter is not protected.
//! The same program in C compiles without a warning (ex18, switch 1 OFF).
//! Fixes: `critical_section::Mutex<RefCell<_>>` or atomics (src/main.rs).
#![no_std]
#![no_main]

use core::cell::RefCell;
use panic_halt as _;
use rp_pico::entry;
use rp_pico::hal::pac::{self, interrupt};

#[derive(Clone, Copy)]
struct Sample {
    w: [u32; 8],
}

static SHARED: RefCell<Sample> = RefCell::new(Sample { w: [0; 8] });

#[interrupt]
fn IO_IRQ_BANK0() {
    SHARED.borrow_mut().w = [1; 8]; // the ISR writes ...
}

#[entry]
fn main() -> ! {
    let _ = pac::Peripherals::take();
    loop {
        let copy = *SHARED.borrow(); // ... while main reads
        core::hint::black_box(copy);
    }
}
