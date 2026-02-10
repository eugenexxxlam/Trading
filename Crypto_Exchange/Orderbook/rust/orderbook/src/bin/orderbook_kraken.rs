use std::{
    collections::BTreeMap,
    io::Write,
    sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Arc, Mutex},
    thread,
    time::{Duration, Instant},
};
use chrono::{FixedOffset, Utc};
use tungstenite::{connect, Message};

type OrderBookState = Arc<Mutex<KrakenBook>>;

#[derive(Debug, Clone)]
struct Level {
    price: f64,
    volume: f64,
}

struct KrakenBook {
    bids: BTreeMap<i64, f64>, // price * 100 as i64 -> volume
    asks: BTreeMap<i64, f64>, // price * 100 as i64 -> volume
    snapshot_received: bool,
}

impl KrakenBook {
    fn new() -> Self {
        Self {
            bids: BTreeMap::new(),
            asks: BTreeMap::new(),
            snapshot_received: false,
        }
    }

    fn process_message(&mut self, msg: &str) {
        // Kraken sends: [channelID, {data}, "book-10", "XBT/USD"]
        // Snapshot: {"as": [["price","vol","timestamp"],...], "bs": [...]}
        // Update: {"a": [["price","vol","timestamp"],...], "b": [...]}
        
        // Try parsing as array response
        if let Ok(data) = serde_json::from_str::<serde_json::Value>(msg) {
            if let Some(array) = data.as_array() {
                if array.len() >= 2 {
                    if let Some(book_data) = array[1].as_object() {
                        // Check for snapshot
                        if book_data.contains_key("as") && book_data.contains_key("bs") {
                            self.process_snapshot(book_data);
                        } else if self.snapshot_received {
                            // Process incremental update
                            self.process_update(book_data);
                        }
                    }
                }
            }
        }
    }

    fn process_snapshot(&mut self, book_data: &serde_json::Map<String, serde_json::Value>) {
        self.bids.clear();
        self.asks.clear();

        // Process bids ("bs")
        if let Some(bs) = book_data.get("bs").and_then(|v| v.as_array()) {
            for level in bs {
                if let Some(arr) = level.as_array() {
                    if arr.len() >= 2 {
                        if let (Some(price_str), Some(vol_str)) = (arr[0].as_str(), arr[1].as_str()) {
                            if let (Ok(price), Ok(volume)) = (price_str.parse::<f64>(), vol_str.parse::<f64>()) {
                                if volume > 0.0 {
                                    let price_key = (price * 100.0).round() as i64;
                                    self.bids.insert(price_key, volume);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Process asks ("as")
        if let Some(as_) = book_data.get("as").and_then(|v| v.as_array()) {
            for level in as_ {
                if let Some(arr) = level.as_array() {
                    if arr.len() >= 2 {
                        if let (Some(price_str), Some(vol_str)) = (arr[0].as_str(), arr[1].as_str()) {
                            if let (Ok(price), Ok(volume)) = (price_str.parse::<f64>(), vol_str.parse::<f64>()) {
                                if volume > 0.0 {
                                    let price_key = (price * 100.0).round() as i64;
                                    self.asks.insert(price_key, volume);
                                }
                            }
                        }
                    }
                }
            }
        }

        self.snapshot_received = true;
        self.sanitize();
    }

    fn process_update(&mut self, book_data: &serde_json::Map<String, serde_json::Value>) {
        // Process bid updates ("b")
        if let Some(b) = book_data.get("b").and_then(|v| v.as_array()) {
            for level in b {
                if let Some(arr) = level.as_array() {
                    if arr.len() >= 2 {
                        if let (Some(price_str), Some(vol_str)) = (arr[0].as_str(), arr[1].as_str()) {
                            if let (Ok(price), Ok(volume)) = (price_str.parse::<f64>(), vol_str.parse::<f64>()) {
                                let price_key = (price * 100.0).round() as i64;
                                if volume == 0.0 {
                                    self.bids.remove(&price_key);
                                } else {
                                    self.bids.insert(price_key, volume);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Process ask updates ("a")
        if let Some(a) = book_data.get("a").and_then(|v| v.as_array()) {
            for level in a {
                if let Some(arr) = level.as_array() {
                    if arr.len() >= 2 {
                        if let (Some(price_str), Some(vol_str)) = (arr[0].as_str(), arr[1].as_str()) {
                            if let (Ok(price), Ok(volume)) = (price_str.parse::<f64>(), vol_str.parse::<f64>()) {
                                let price_key = (price * 100.0).round() as i64;
                                if volume == 0.0 {
                                    self.asks.remove(&price_key);
                                } else {
                                    self.asks.insert(price_key, volume);
                                }
                            }
                        }
                    }
                }
            }
        }

        self.sanitize();
    }

    fn sanitize(&mut self) {
        // Remove crossed levels
        if !self.bids.is_empty() && !self.asks.is_empty() {
            let best_ask = *self.asks.iter().next().unwrap().0;

            // Remove bids >= best_ask
            self.bids.retain(|&price_key, _| price_key < best_ask);
            
            // Remove asks <= best_bid (recalculate after bids cleanup)
            if let Some((&new_best_bid, _)) = self.bids.iter().rev().next() {
                self.asks.retain(|&price_key, _| price_key > new_best_bid);
            }
        }
    }

    fn get_bids(&self, depth: usize) -> Vec<Level> {
        let best_ask_price = self.asks.iter().next().map(|(k, _)| *k).unwrap_or(i64::MAX);
        
        self.bids
            .iter()
            .rev()
            .filter(|(price_key, _)| **price_key < best_ask_price)
            .take(depth)
            .map(|(price_key, volume)| Level {
                price: *price_key as f64 / 100.0,
                volume: *volume,
            })
            .collect()
    }

    fn get_asks(&self, depth: usize) -> Vec<Level> {
        let best_bid_price = self.bids.iter().rev().next().map(|(k, _)| *k).unwrap_or(0);
        
        self.asks
            .iter()
            .filter(|(price_key, _)| **price_key > best_bid_price)
            .take(depth)
            .map(|(price_key, volume)| Level {
                price: *price_key as f64 / 100.0,
                volume: *volume,
            })
            .collect()
    }
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

fn render_orderbook(bids: &[Level], asks: &[Level], ups: f64, total: u64) -> String {
    let now = Utc::now();
    let gmt8 = now.with_timezone(&FixedOffset::east_opt(8 * 3600).unwrap());
    let (best_bid, best_ask) = (bids.first().map(|l| l.price).unwrap_or(0.0), asks.first().map(|l| l.price).unwrap_or(0.0));
    let (spread, mid) = (best_ask - best_bid, (best_bid + best_ask) / 2.0);
    let (sep, div) = ("=".repeat(70), "-".repeat(70));
    
    let mut out = format!(
        "{CLEAR}{sep}\n{:^70}\n{:^70}\n{:^70}\n{:^70}\n{YELLOW}{:^70}{RESET}\n{sep}\n\n",
        "Kraken XBT/USD Orderbook (Rust)",
        format!("GMT: {}", now.format("%Y-%m-%d %H:%M:%S")),
        format!("GMT+8: {}", gmt8.format("%Y-%m-%d %H:%M:%S")),
        format!("Spread: ${:.2} | Mid: ${}", spread, format_price(mid)),
        format!("Updates/sec: {:.1} | Total: {}", ups, format_commas(total as i64))
    );
    
    let header = |title| format!("{:^70}\n{:>11}  {:>10}  {:>9}  {:>10}  {:>9}\n{div}\n", 
        title, "Price", "BTC", "USDT", "Cum BTC", "Cum USDT");
    
    let row = |c: u8, p: f64, q: f64, n: f64, cb: f64, cu: f64| 
        format!("\x1b[{c}m{:>11}\x1b[0m  {:>10.7}  {:>9}  {:>10.4}  {:>9}\n",
            format_price(p), q, format_commas(n as i64), cb, format_commas(cu as i64));
    
    let padding_row = |cb: f64, cu: f64| 
        format!("{:>11}  {:>10}  {:>9}  {:>10.4}  {:>9}\n",
            "---", "---", "---", cb, format_commas(cu as i64));
    
    // ASKS section - always 16 levels
    out.push_str(&header("ASKS (Sell Orders)"));
    let mut cum = (0.0, 0.0);
    let ask_rows: Vec<_> = asks.iter().take(16).map(|l| {
        cum = (cum.0 + l.volume, cum.1 + l.price * l.volume);
        (l.price, l.volume, l.price * l.volume, cum.0, cum.1)
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
            let l = &bids[i];
            cum = (cum.0 + l.volume, cum.1 + l.price * l.volume);
            out.push_str(&row(GREEN, l.price, l.volume, l.price * l.volume, cum.0, cum.1));
        } else {
            out.push_str(&padding_row(cum.0, cum.1));
        }
    }
    
    out.push_str(&format!("{sep}\n"));
    out
}

fn main() {
    let running = Arc::new(AtomicBool::new(true));
    let counter = Arc::new(AtomicU64::new(0));
    let r = running.clone();
    
    ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    })
    .expect("Error setting Ctrl-C handler");

    let orderbook: OrderBookState = Arc::new(Mutex::new(KrakenBook::new()));
    let ob_clone = orderbook.clone();
    let running_clone = running.clone();
    let counter_clone = counter.clone();

    // WebSocket thread
    thread::spawn({
        let (r, b, c) = (Arc::clone(&running_clone), Arc::clone(&ob_clone), Arc::clone(&counter_clone));
        move || {
            const URL: &str = "wss://ws.kraken.com/";
            while r.load(Ordering::SeqCst) {
                if let Ok((mut ws, _)) = connect(URL) {
                    let subscribe = serde_json::json!({
                        "event": "subscribe",
                        "pair": ["XBT/USD"],
                        "subscription": {"name": "book", "depth": 10}
                    });
                    
                    if ws.send(Message::Text(subscribe.to_string())).is_ok() {
                        println!("✓ Kraken connected");
                    }
                    
                    while r.load(Ordering::SeqCst) {
                        match ws.read() {
                            Ok(Message::Text(t)) => {
                                if !t.contains("\"event\"") {
                                    if let Ok(mut book) = b.lock() {
                                        book.process_message(&t);
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
                    eprintln!("Kraken error, reconnecting...");
                    thread::sleep(Duration::from_secs(2));
                }
            }
        }
    });
    
    thread::sleep(Duration::from_millis(500));
    let (mut last, mut timer, mut ups) = (0u64, Instant::now(), 0.0);
    
    while running.load(Ordering::SeqCst) {
        if let Ok(book) = orderbook.lock() {
            let bids = book.get_bids(16);
            let asks = book.get_asks(16);
            
            if !bids.is_empty() && !asks.is_empty() {
                let (curr, elapsed) = (counter.load(Ordering::Relaxed), timer.elapsed().as_secs_f64());
                if elapsed >= 1.0 {
                    ups = (curr - last) as f64 / elapsed;
                    (last, timer) = (curr, Instant::now());
                }
                print!("{}", render_orderbook(&bids, &asks, ups, curr));
                std::io::stdout().flush().ok();
            }
        }
        thread::sleep(Duration::from_millis(50));
    }
    println!("\n✓ Stopped");
}
