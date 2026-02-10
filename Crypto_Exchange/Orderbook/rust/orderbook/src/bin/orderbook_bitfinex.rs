//! Bitfinex tBTCUSD orderbook - WebSocket live display at 20 FPS
//! 
//! Bitfinex uses INCREMENTAL UPDATES (snapshot + delta updates)
//! BTreeMap is required: O(log n) insert/update/delete, maintains sorted order automatically
//! Cannot use Vec: would require O(n) search + O(n log n) sort on every update

use std::{
    collections::BTreeMap,
    io::Write,
    sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Arc, Mutex},
    thread,
    time::{Duration, Instant},
};
use chrono::{FixedOffset, Utc};
use serde_json::Value;
use tungstenite::{connect, Message};

type Book = (Vec<(f64, f64)>, Vec<(f64, f64)>);

#[derive(Debug)]
struct OrderBookState {
    bids: BTreeMap<i64, f64>,  // price_key -> volume (use i64 for precise ordering)
    asks: BTreeMap<i64, f64>,  // price_key -> volume
}

impl OrderBookState {
    fn new() -> Self {
        Self {
            bids: BTreeMap::new(),
            asks: BTreeMap::new(),
        }
    }
    
    fn to_book(&self) -> Option<Book> {
        if self.bids.is_empty() || self.asks.is_empty() {
            return None;
        }
        
        // Convert BTreeMap to sorted vectors
        let mut bids: Vec<(f64, f64)> = self.bids.iter()
            .rev()  // Reverse for descending order
            .map(|(&price_key, &vol)| (price_key as f64 / 100.0, vol))
            .collect();
        
        let mut asks: Vec<(f64, f64)> = self.asks.iter()
            .map(|(&price_key, &vol)| (price_key as f64 / 100.0, vol))
            .collect();
        
        // Sanitize crossed levels
        if let (Some(&(best_bid, _)), Some(&(best_ask, _))) = (bids.first(), asks.first()) {
            bids.retain(|&(p, _)| p < best_ask);
            asks.retain(|&(p, _)| p > best_bid);
        }
        
        if !bids.is_empty() && !asks.is_empty() {
            Some((bids, asks))
        } else {
            None
        }
    }
}

fn price_to_key(price: f64) -> i64 {
    (price * 100.0).round() as i64
}

fn process_message(state: &mut OrderBookState, json: &str) -> bool {
    let val: Value = match serde_json::from_str(json) {
        Ok(v) => v,
        Err(_) => return false,
    };
    
    // Skip event messages (objects)
    if val.is_object() {
        return false;
    }
    
    // Must be array: [channel_id, data]
    let arr = match val.as_array() {
        Some(a) if a.len() >= 2 => a,
        _ => return false,
    };
    
    let data = &arr[1];
    
    // Check if snapshot (nested array) or update (single array)
    if let Some(snapshot) = data.as_array() {
        if snapshot.is_empty() {
            return false;
        }
        
        // Snapshot: [[price, count, amount], ...]
        if snapshot[0].is_array() {
            state.bids.clear();
            state.asks.clear();
            
            for entry in snapshot {
                if let Some(level) = entry.as_array() {
                    if level.len() == 3 {
                        if let (Some(price), Some(_count), Some(amount)) = 
                            (level[0].as_f64(), level[1].as_f64(), level[2].as_f64()) {
                            let price_key = price_to_key(price);
                            if amount > 0.0 {
                                state.bids.insert(price_key, amount);
                            } else {
                                state.asks.insert(price_key, amount.abs());
                            }
                        }
                    }
                }
            }
            return true;
        }
        
        // Update: [price, count, amount]
        if snapshot.len() == 3 {
            if let (Some(price), Some(count), Some(amount)) = 
                (snapshot[0].as_f64(), snapshot[1].as_f64(), snapshot[2].as_f64()) {
                let price_key = price_to_key(price);
                
                if count > 0.0 {
                    // Update level
                    if amount > 0.0 {
                        state.bids.insert(price_key, amount);
                    } else {
                        state.asks.insert(price_key, amount.abs());
                    }
                } else {
                    // Delete level (count == 0)
                    if amount > 0.0 {
                        state.bids.remove(&price_key);
                    } else {
                        state.asks.remove(&price_key);
                    }
                }
                return true;
            }
        }
    }
    
    false
}

fn format_commas(n: i64) -> String {
    n.to_string().as_bytes().rchunks(3).rev()
        .map(|c| std::str::from_utf8(c).unwrap())
        .collect::<Vec<_>>().join(",")
}

fn format_price(price: f64) -> String {
    let (int, dec) = (price as i64, (price.fract().abs() * 100.0).round() as i64);
    format!("{}.{:02}", format_commas(int), dec)
}

const CLEAR: &str = "\x1b[H\x1b[J";
const RED: u8 = 91;
const GREEN: u8 = 92;
const YELLOW: &str = "\x1b[93m";
const RESET: &str = "\x1b[0m";

fn render_orderbook(bids: &[(f64, f64)], asks: &[(f64, f64)], ups: f64, total: u64) -> String {
    let now = Utc::now();
    let gmt8 = now.with_timezone(&FixedOffset::east_opt(8 * 3600).unwrap());
    let (best_bid, best_ask) = (bids.first().map(|l| l.0).unwrap_or(0.0), asks.first().map(|l| l.0).unwrap_or(0.0));
    let (spread, mid) = (best_ask - best_bid, (best_bid + best_ask) / 2.0);
    let (sep, div) = ("=".repeat(70), "-".repeat(70));
    
    let mut out = format!(
        "{CLEAR}{sep}\n{:^70}\n{:^70}\n{:^70}\n{:^70}\n{YELLOW}{:^70}{RESET}\n{sep}\n\n",
        "Bitfinex tBTCUSD Orderbook (Rust)",
        format!("GMT: {}", now.format("%Y-%m-%d %H:%M:%S")),
        format!("GMT+8: {}", gmt8.format("%Y-%m-%d %H:%M:%S")),
        format!("Spread: ${:.2} | Mid: ${}", spread, format_price(mid)),
        format!("Updates/sec: {:.1} | Total: {}", ups, format_commas(total as i64))
    );
    
    let header = |title| format!("{:^70}\n{:>11}  {:>10}  {:>9}  {:>10}  {:>9}\n{div}\n", 
        title, "Price", "BTC", "USD", "Cum BTC", "Cum USD");
    
    let row = |c: u8, p: f64, q: f64, n: f64, cb: f64, cu: f64| 
        format!("\x1b[{c}m{:>11}\x1b[0m  {:>10.7}  {:>9}  {:>10.4}  {:>9}\n",
            format_price(p), q, format_commas(n as i64), cb, format_commas(cu as i64));
    
    let padding_row = |cb: f64, cu: f64| 
        format!("{:>11}  {:>10}  {:>9}  {:>10.4}  {:>9}\n",
            "---", "---", "---", cb, format_commas(cu as i64));
    
    // ASKS section - always 16 levels
    out.push_str(&header("ASKS (Sell Orders)"));
    let mut cum = (0.0, 0.0);
    let ask_rows: Vec<_> = asks.iter().take(16).map(|&(p, q)| {
        cum = (cum.0 + q, cum.1 + p * q);
        (p, q, p * q, cum.0, cum.1)
    }).collect();
    
    // Display asks in reverse (highest first)
    for i in (0..16).rev() {
        if i < ask_rows.len() {
            let (p, q, n, cb, cu) = ask_rows[i];
            out.push_str(&row(RED, p, q, n, cb, cu));
        } else {
            out.push_str(&padding_row(cum.0, cum.1));
        }
    }
    
    // BIDS section - always 16 levels
    out.push_str(&format!("\n{}", header("BIDS (Buy Orders)")));
    cum = (0.0, 0.0);
    for i in 0..16 {
        if i < bids.len() {
            let (p, q) = bids[i];
            cum = (cum.0 + q, cum.1 + p * q);
            out.push_str(&row(GREEN, p, q, p * q, cum.0, cum.1));
        } else {
            out.push_str(&padding_row(cum.0, cum.1));
        }
    }
    
    out.push_str(&format!("{sep}\n"));
    out
}

fn main() {
    let (running, book, counter, state) = (
        Arc::new(AtomicBool::new(true)),
        Arc::new(Mutex::new((vec![], vec![]))),
        Arc::new(AtomicU64::new(0)),
        Arc::new(Mutex::new(OrderBookState::new()))
    );
    
    ctrlc::set_handler({
        let r = Arc::clone(&running);
        move || r.store(false, Ordering::SeqCst)
    }).expect("Failed to set Ctrl+C handler");
    
    thread::spawn({
        let (r, b, c, s) = (Arc::clone(&running), Arc::clone(&book), Arc::clone(&counter), Arc::clone(&state));
        move || {
            const URL: &str = "wss://api-pub.bitfinex.com/ws/2";
            while r.load(Ordering::SeqCst) {
                if let Ok((mut ws, _)) = connect(URL) {
                    // Subscribe to order book
                    let sub = r#"{"event":"subscribe","channel":"book","symbol":"tBTCUSD","prec":"P0","freq":"F0","len":"25"}"#;
                    if ws.send(Message::Text(sub.to_string())).is_ok() {
                        println!("✓ Bitfinex connected");
                    }
                    
                    while r.load(Ordering::SeqCst) {
                        match ws.read() {
                            Ok(Message::Text(t)) => {
                                let mut state_lock = s.lock().unwrap();
                                if process_message(&mut state_lock, &t) {
                                    if let Some(bk) = state_lock.to_book() {
                                        *b.lock().unwrap() = bk;
                                        c.fetch_add(1, Ordering::Relaxed);
                                    }
                                }
                            },
                            Ok(Message::Ping(d)) => { let _ = ws.send(Message::Pong(d)); },
                            Ok(Message::Close(_)) | Err(_) => break,
                            _ => {}
                        }
                    }
                }
                if r.load(Ordering::SeqCst) {
                    println!("Bitfinex error, reconnecting...");
                    thread::sleep(Duration::from_secs(2));
                }
            }
        }
    });
    
    thread::sleep(Duration::from_millis(500));
    let (mut last, mut timer, mut ups) = (0u64, Instant::now(), 0.0);
    
    while running.load(Ordering::SeqCst) {
        let (bids, asks) = book.lock().unwrap().clone();
        if !bids.is_empty() {
            let (curr, elapsed) = (counter.load(Ordering::Relaxed), timer.elapsed().as_secs_f64());
            if elapsed >= 1.0 {
                ups = (curr - last) as f64 / elapsed;
                (last, timer) = (curr, Instant::now());
            }
            print!("{}", render_orderbook(&bids, &asks, ups, curr));
            std::io::stdout().flush().ok();
        }
        thread::sleep(Duration::from_millis(50));
    }
    println!("\n✓ Stopped");
}
