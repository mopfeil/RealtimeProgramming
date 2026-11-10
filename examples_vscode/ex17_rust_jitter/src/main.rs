//! Periodic job in Rust: period, jitter and the price of the heap.
//! The same job and the same output as ex15 (C) and ex16 (MicroPython).
//!
//! Every 2000 us the job takes a new sample (pseudo random), runs a 16-tap
//! FIR filter over the last 16 samples and builds a log record. Every 500
//! periods the program prints statistics (outside of the measurement):
//!
//!   heap=0 rel=0 gc=- period min/avg/max 2000/2000/2000 us job min/max 6/7 us overruns 0
//!
//!   DIP switch 1 ON (GP10 LOW): the log record is a `String` on the heap,
//!     the window a `Vec` with a varying capacity; eight records stay alive,
//!     the oldest is dropped (freed) when its slot is reused.
//!     OFF: the record is written into a fixed buffer, no heap at all.
//!   DIP switch 2 ON (GP11 LOW): relative timing, "work, then wait 2000 us".
//!     OFF: absolute timing, "wait until the next multiple of 2000 us".
//!   (DIP switch 3 is used by the MicroPython version only.)
//!
//! Logic analyzer: D0 = GP2 (HIGH during the job), D1 = GP3 (HIGH during
//! allocation and drop). Output on UART0 (GP0/GP1).
//!
//!     cargo build --release      then F1 -> "Wokwi: Start Simulator"
#![no_std]
#![no_main]

extern crate alloc;

use alloc::{string::String, vec::Vec};
use core::fmt::Write;
use embedded_alloc::LlffHeap as Heap;
use embedded_hal::digital::{InputPin, OutputPin};
use panic_halt as _;
use rp_pico::entry;
use rp_pico::hal::fugit::RateExtU32;
use rp_pico::hal::uart::{DataBits, StopBits, UartConfig, UartPeripheral};
use rp_pico::hal::{self, Clock, gpio, pac};

#[global_allocator]
static HEAP: Heap = Heap::empty();

const PERIOD_US: u32 = 2000;
const REPORT_EVERY: u32 = 500;
const TAPS: usize = 16;
const COEFF: [i32; TAPS] = [1, 3, 7, 12, 18, 24, 28, 31, 31, 28, 24, 18, 12, 7, 3, 1];

/// The state of the job. All of it is known at compile time: no heap needed.
struct Filter {
    samples: [i16; TAPS], // ring buffer of the last 16 samples
    head: usize,
    rng: u32,
    record: FixedBuf,           // record without heap (switch 1 OFF)
    records: [Option<String>; 8], // records on the heap (switch 1 ON)
}

/// A fixed-size text buffer: `write!` into it never allocates.
struct FixedBuf {
    data: [u8; 64],
    len: usize,
}

impl Write for FixedBuf {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let n = s.len().min(self.data.len() - self.len); // cut, never overflow
        self.data[self.len..self.len + n].copy_from_slice(&s.as_bytes()[..n]);
        self.len += n;
        Ok(())
    }
}

impl Filter {
    fn next_sample(&mut self) -> i16 {
        self.rng = (self.rng * 75 + 74) % 65537; // the LCG of ex15/ex16
        (self.rng & 0x0FFF) as i16 - 2048
    }

    fn fir(&self) -> i32 {
        (0..TAPS)
            .map(|i| COEFF[i] * self.samples[(self.head + i) % TAPS] as i32)
            .sum()
    }

    fn job(&mut self, n: u32, use_heap: bool, heap_pin: &mut impl OutputPin) -> i32 {
        self.samples[self.head] = self.next_sample();
        self.head = (self.head + 1) % TAPS;
        let acc = self.fir();
        if use_heap {
            heap_pin.set_high().ok();
            let len = 16 + (self.rng % 400) as usize; // varying size: fragments the heap
            let window: Vec<i16> = Vec::with_capacity(len);
            let mut rec = String::with_capacity(len);
            write!(rec, "y={acc}").ok();
            // assigning drops (frees) the record that was in this slot
            self.records[(n % 8) as usize] = Some(rec);
            drop(window);
            heap_pin.set_low().ok();
        } else {
            self.record.len = 0;
            write!(self.record, "y={acc}").ok();
        }
        acc
    }
}

#[entry]
fn main() -> ! {
    {
        use core::mem::MaybeUninit;
        const HEAP_SIZE: usize = 16 * 1024;
        static mut HEAP_MEM: [MaybeUninit<u8>; HEAP_SIZE] = [MaybeUninit::uninit(); HEAP_SIZE];
        unsafe { HEAP.init(&raw mut HEAP_MEM as usize, HEAP_SIZE) }
    }
    let mut pac = pac::Peripherals::take().unwrap();
    let mut watchdog = hal::Watchdog::new(pac.WATCHDOG);
    let clocks = hal::clocks::init_clocks_and_plls(
        rp_pico::XOSC_CRYSTAL_FREQ, pac.XOSC, pac.CLOCKS, pac.PLL_SYS, pac.PLL_USB,
        &mut pac.RESETS, &mut watchdog,
    )
    .ok()
    .unwrap();
    let sio = hal::Sio::new(pac.SIO);
    let pins = rp_pico::Pins::new(pac.IO_BANK0, pac.PADS_BANK0, sio.gpio_bank0, &mut pac.RESETS);
    let uart_pins = (
        pins.gpio0.into_function::<gpio::FunctionUart>(),
        pins.gpio1.into_function::<gpio::FunctionUart>(),
    );
    let mut uart = UartPeripheral::new(pac.UART0, uart_pins, &mut pac.RESETS)
        .enable(
            UartConfig::new(115_200.Hz(), DataBits::Eight, None, StopBits::One),
            clocks.peripheral_clock.freq(),
        )
        .unwrap();
    // 1 MHz timer: get_counter_low() counts microseconds (wraps after 71 min)
    let timer = hal::Timer::new(pac.TIMER, &mut pac.RESETS, &clocks);
    let now = || timer.get_counter_low();

    let mut job_pin = pins.gpio2.into_push_pull_output();
    let mut heap_pin = pins.gpio3.into_push_pull_output();
    let mut sw_heap = pins.gpio10.into_pull_up_input();
    let mut sw_rel = pins.gpio11.into_pull_up_input();

    let mut f = Filter {
        samples: [0; TAPS],
        head: 0,
        rng: 12345,
        record: FixedBuf { data: [0; 64], len: 0 },
        records: Default::default(),
    };
    writeln!(uart, "ex17 Rust: periodic job, 2000 us\r").ok();

    loop {
        let use_heap = sw_heap.is_low().unwrap();
        let relative = sw_rel.is_low().unwrap();
        let (mut p_min, mut p_max, mut j_min, mut j_max, mut overruns) = (u32::MAX, 0, u32::MAX, 0, 0);

        let mut next = now().wrapping_add(PERIOD_US);
        while (now().wrapping_sub(next) as i32) < 0 {} // start on a period boundary
        let first = now();
        let mut last = first;

        for n in 0..=REPORT_EVERY {
            let start = now();
            if n > 0 {
                let p = start.wrapping_sub(last); // period since the last start
                p_min = p_min.min(p);
                p_max = p_max.max(p);
                if p > PERIOD_US + PERIOD_US / 10 {
                    overruns += 1;
                }
            }
            last = start;
            job_pin.set_high().ok();
            core::hint::black_box(f.job(n, use_heap, &mut heap_pin));
            job_pin.set_low().ok();
            let d = now().wrapping_sub(start);
            j_min = j_min.min(d);
            j_max = j_max.max(d);

            if relative {
                let t = now(); // work, then wait
                while now().wrapping_sub(t) < PERIOD_US {}
            } else {
                next = next.wrapping_add(PERIOD_US); // wait until the deadline
                while (now().wrapping_sub(next) as i32) < 0 {}
            }
        }
        let avg = last.wrapping_sub(first) / REPORT_EVERY;
        writeln!(
            uart,
            "heap={} rel={} gc=- period min/avg/max {p_min}/{avg}/{p_max} us job min/max {j_min}/{j_max} us overruns {overruns}\r",
            use_heap as u8, relative as u8
        )
        .ok();
    }
}
