//! Interrupt and shared data in Rust: what the compiler allows, and what it costs.
//! The same experiment as ex18 (C) and ex19 (MicroPython).
//!
//! PWM slice 2 drives GP5 with 1 kHz; a wire takes the signal back to GP6.
//! Each rising edge on GP6 raises the interrupt IO_IRQ_BANK0. The PWM counter
//! runs at 1 MHz and starts at 0 on the edge, so the counter value that the
//! ISR reads is its own latency in microseconds.
//!
//! The ISR writes a sample of 8 words, all with the same sequence number.
//! The main loop copies the sample and "processes" every word (10 us each).
//! A copy whose words differ is torn.
//!
//! Safe Rust cannot share a plain variable between main and an ISR (see
//! examples/race.rs, which the compiler rejects). There are two ways:
//!   * `critical_section::Mutex<RefCell<T>>`: access only inside
//!     `critical_section::with()`, i.e. with interrupts disabled.
//!   * `static mut` plus `unsafe`: the programmer promises that it is safe.
//!     Here it is not: this is the torn-read version, written on purpose.
//!
//!   DIP switch 1 OFF (GP10 HIGH): `static mut` + `unsafe`: torn copies.
//!   DIP switch 1 ON  (GP10 LOW) : `Mutex<RefCell<Sample>>`.
//!     DIP switch 2 OFF: long critical section, copy and process inside.
//!     DIP switch 2 ON : short critical section, copy inside, process after.
//!
//! Output every second on UART0 (GP0/GP1), e.g.
//!   protect=0 short=0 irqs 1000 copies 12500 torn 1000 latency min/max 1/1 us
//!
//! Logic analyzer: D0 = GP5 (PWM, the interrupt source), D1 = GP3 (HIGH
//! during the ISR), D2 = GP2 (HIGH while main copies/processes).
//!
//!     cargo build --release      then F1 -> "Wokwi: Start Simulator"
#![no_std]
#![no_main]

use core::cell::RefCell;
use core::fmt::Write;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use critical_section::Mutex;
use embedded_hal::digital::{InputPin, OutputPin};
use panic_halt as _;
use rp_pico::entry;
use rp_pico::hal::fugit::RateExtU32;
use rp_pico::hal::gpio::{self, Interrupt::EdgeHigh};
use rp_pico::hal::pac::{self, interrupt};
use rp_pico::hal::uart::{DataBits, StopBits, UartConfig, UartPeripheral};
use rp_pico::hal::{self, Clock};

const WORDS: usize = 8;

#[derive(Clone, Copy)]
struct Sample {
    w: [u32; WORDS],
}

/// Statistics of the ISR, read and reset by main.
#[derive(Clone, Copy)]
struct Stats {
    irqs: u32,
    lat_min: u32,
    lat_max: u32,
}
const STATS_RESET: Stats = Stats { irqs: 0, lat_min: u32::MAX, lat_max: 0 };

type IrqPin = gpio::Pin<gpio::bank0::Gpio6, gpio::FunctionSioInput, gpio::PullNone>;
type IsrPin = gpio::Pin<gpio::bank0::Gpio3, gpio::FunctionSioOutput, gpio::PullDown>;

// Safe sharing: every access needs a critical-section token `cs`.
static SHARED: Mutex<RefCell<Sample>> = Mutex::new(RefCell::new(Sample { w: [0; WORDS] }));
static STATS: Mutex<RefCell<Stats>> = Mutex::new(RefCell::new(STATS_RESET));
// Peripherals that only the ISR uses, moved there by main.
static ISR_PINS: Mutex<RefCell<Option<(IrqPin, IsrPin)>>> = Mutex::new(RefCell::new(None));
// Single words can be shared without a lock: atomics.
static PROTECT: AtomicBool = AtomicBool::new(false);
static SEQ: AtomicU32 = AtomicU32::new(0);
// Unsafe sharing: the compiler cannot check it, `unsafe` is required.
static mut RAW: Sample = Sample { w: [0; WORDS] };

#[interrupt]
fn IO_IRQ_BANK0() {
    // The ISR cannot be interrupted by main, but it takes the critical
    // section anyway: Mutex::borrow() needs the token.
    critical_section::with(|cs| {
        let mut pins = ISR_PINS.borrow_ref_mut(cs);
        let Some((irq_pin, isr_pin)) = pins.as_mut() else { return };
        isr_pin.set_high().ok();
        // PWM slice 2 counter: microseconds since the edge
        let lat = unsafe { (*pac::PWM::ptr()).ch(2).ctr().read().bits() };
        let mut s = STATS.borrow_ref_mut(cs);
        s.irqs += 1;
        s.lat_min = s.lat_min.min(lat);
        s.lat_max = s.lat_max.max(lat);

        // The Cortex-M0+ has no atomic read-modify-write instructions, so
        // there is no SEQ.fetch_add() (compile error). Load and store are
        // atomic; this is safe because only the ISR writes SEQ.
        let seq = SEQ.load(Ordering::Relaxed) + 1;
        SEQ.store(seq, Ordering::Relaxed);
        if PROTECT.load(Ordering::Relaxed) {
            SHARED.borrow_ref_mut(cs).w = [seq; WORDS];
        } else {
            // SAFETY: none. main may be in the middle of reading RAW.
            let raw = &raw mut RAW;
            for i in 0..WORDS {
                unsafe { (&raw mut (*raw).w[i]).write_volatile(seq) };
            }
        }
        irq_pin.clear_interrupt(EdgeHigh);
        isr_pin.set_low().ok();
    });
}

#[entry]
fn main() -> ! {
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
    let timer = hal::Timer::new(pac.TIMER, &mut pac.RESETS, &clocks);
    let now = || timer.get_counter_low();
    let busy_wait_us = |us: u32| {
        let t = now();
        while now().wrapping_sub(t) < us {}
    };
    let process = |w: u32| {
        busy_wait_us(10); // stands for real work on each word
        w
    };

    let mut main_pin = pins.gpio2.into_push_pull_output();
    let mut sw_protect = pins.gpio10.into_pull_up_input();
    let mut sw_short = pins.gpio11.into_pull_up_input();

    // PWM: 1 MHz counter (125 MHz / 125), period 1000 us, HIGH for 500 us
    let mut slices = hal::pwm::Slices::new(pac.PWM, &mut pac.RESETS);
    let pwm = &mut slices.pwm2;
    pwm.set_div_int(125);
    pwm.set_top(999);
    pwm.channel_b.output_to(pins.gpio5);
    embedded_hal::pwm::SetDutyCycle::set_duty_cycle(&mut pwm.channel_b, 500).ok();

    let irq_pin: IrqPin = pins.gpio6.reconfigure();
    irq_pin.set_interrupt_enabled(EdgeHigh, true);
    let isr_pin: IsrPin = pins.gpio3.into_push_pull_output();
    critical_section::with(|cs| ISR_PINS.replace(cs, Some((irq_pin, isr_pin))));
    unsafe { pac::NVIC::unmask(pac::Interrupt::IO_IRQ_BANK0) };
    pwm.enable();
    writeln!(uart, "ex20 Rust: ISR and shared data\r").ok();

    let (mut copies, mut torn, mut last_report) = (0u32, 0u32, now());
    loop {
        let protect = sw_protect.is_low().unwrap();
        let short_cs = sw_short.is_low().unwrap();
        PROTECT.store(protect, Ordering::Relaxed);
        let mut copy = [0u32; WORDS];

        main_pin.set_high().ok();
        if !protect {
            let raw = &raw const RAW;
            for i in 0..WORDS {
                // SAFETY: none. The ISR may write RAW between two words.
                copy[i] = process(unsafe { (&raw const (*raw).w[i]).read_volatile() });
            }
        } else if !short_cs {
            critical_section::with(|cs| {
                // long critical section: interrupts stay off while processing
                let s = SHARED.borrow_ref(cs);
                for i in 0..WORDS {
                    copy[i] = process(s.w[i]);
                }
            });
        } else {
            let s = critical_section::with(|cs| *SHARED.borrow_ref(cs)); // short: copy only
            for i in 0..WORDS {
                copy[i] = process(s.w[i]);
            }
        }
        main_pin.set_low().ok();

        copies += 1;
        if copy.iter().any(|&w| w != copy[0]) {
            torn += 1;
        }

        if now().wrapping_sub(last_report) >= 1_000_000 {
            last_report = now();
            let s = critical_section::with(|cs| STATS.replace(cs, STATS_RESET));
            writeln!(
                uart,
                "protect={} short={} irqs {} copies {copies} torn {torn} latency min/max {}/{} us\r",
                protect as u8, short_cs as u8, s.irqs, s.lat_min, s.lat_max
            )
            .ok();
            copies = 0;
            torn = 0;
        }
    }
}
