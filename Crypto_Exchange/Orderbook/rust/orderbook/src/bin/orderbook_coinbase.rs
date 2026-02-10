use chrono::{DateTime, Duration, Utc};
use serde::Deserialize;
use std::collections::BTreeMap;
use std::io::Write;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::{Duration as StdDuration, Instant};
use tungstenite::{connect, Message as WsMessage};

const DEPTH: usize = 16;
const CLEAR: &str = "\x1b[H\x1b[J";

type OrderBookState = Arc<Mutex<CoinbaseBook>>;

#[derive(Debug, Clone)]
struct Level {
    price: f64,
    volume: f64,
}

struct CoinbaseBook {
    bids: BTreeMap<i64, f64>, // price * 100 as i64 -> volume
    asks: BTreeMap<i64, f64>, // price * 100 as i64 -> volume
}

impl CoinbaseBook {
    fn new() -> Self {
        Self {
            bids: BTreeMap::new(),
            asks: BTreeMap::new(),
        }
    }

    fn process_message(&mut self, msg: &str) {
        if let Ok(data) = serde_json::from_str::<CoinbaseResponse>(msg) {
            match data.msg_type.as_str() {
                "snapshot" => {
                    if let Some(bids) = data.bids {
                        self.bids.clear();
                        for level in bids {
                            if let (Ok(price), Ok(volume)) = (level[0].parse::<f64>(), level[1].parse::<f64>()) {
                                if volume > 0.0 {
                                    let price_key = (price * 100.0).round() as i64;
                                    self.bids.insert(price_key, volume);
                                }
                            }
                        }
                    }
                    if let Some(asks) = data.asks {
                        self.asks.clear();
                        for level in asks {
                            if let (Ok(price), Ok(volume)) = (level[0].parse::<f64>(), level[1].parse::<f64>()) {
                                if volume > 0.0 {
                                    let price_key = (price * 100.0).round() as i64;
                                    self.asks.insert(price_key, volume);
                                }
                            }
                        }
                    }
                }
                "l2update" => {
                    if let Some(changes) = data.changes {
                        for change in changes {
                            if change.len() == 3 {
                                let side = &change[0];
                                if let (Ok(price), Ok(size)) = (change[1].parse::<f64>(), change[2].parse::<f64>()) {
                                    let price_key = (price * 100.0).round() as i64;
                                    
                                    if side == "buy" {
                                        if size == 0.0 {
                                            self.bids.remove(&price_key);
                                        } else {
                                            self.bids.insert(price_key, size);
                                        }
                                    } else if side == "sell" {
                                        if size == 0.0 {
                                            self.asks.remove(&price_key);
                                        } else {
                                            self.asks.insert(price_key, size);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                _ => {}
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

#[derive(Debug, Deserialize)]
struct CoinbaseResponse {
    #[serde(rename = "type")]
    msg_type: String,
    #[serde(default)]
    bids: Option<Vec<Vec<String>>>,
    #[serde(default)]
    asks: Option<Vec<Vec<String>>>,
    #[serde(default)]
    changes: Option<Vec<Vec<String>>>,
}

fn format_commas(n: i64) -> String {
    n.to_string()
        .as_bytes()
        .rchunks(3)
        .rev()
        .map(|c| std::str::from_utf8(c).unwrap())
        .collect::<Vec<_>>()
        .join(",")
}

fn render_orderbook(bids: &[Level], asks: &[Level], ups: f64, total: u64) -> String {
    let now_utc: DateTime<Utc> = Utc::now();
    let now_gmt8 = now_utc + Duration::hours(8);

    let spread = if !asks.is_empty() && !bids.is_empty() {
        asks[0].price - bids[0].price
    } else {
        0.0
    };
    
    let mid = if !asks.is_empty() && !bids.is_empty() {
        (asks[0].price + bids[0].price) / 2.0
    } else {
        0.0
    };

    let sep = "=".repeat(70);
    let div = "-".repeat(70);

    let mut output = String::new();
    output.push_str(CLEAR);
    output.push_str(&format!("{sep}\n"));
    output.push_str(&format!("{:^70}\n", "Exchange: Coinbase BTC-USD"));
    output.push_str(&format!("{:^70}\n", format!("GMT: {}", now_utc.format("%Y-%m-%d %H:%M:%S"))));
    output.push_str(&format!("{:^70}\n", format!("GMT+8: {}", now_gmt8.format("%Y-%m-%d %H:%M:%S"))));
    output.push_str(&format!("{:^70}\n", format!("Spread: ${:.2} | Mid: ${:.2}", spread, mid)));
    output.push_str(&format!("\x1b[93m{:^70}\x1b[0m\n", format!("Updates/sec: {:.1} | Total: {}", ups, format_commas(total as i64))));
    output.push_str(&format!("{sep}\n\n"));

    output.push_str(&format!("{:^70}\n", "ASKS (Sell Orders)"));
    output.push_str(&format!("{:>11}  {:>10}  {:>9}  {:>10}  {:>9}\n", "Price", "BTC", "USDT", "Cum BTC", "Cum USDT"));
    output.push_str(&format!("{div}\n"));

    let mut cum_btc_ask = 0.0;
    let mut cum_usdt_ask = 0.0;
    
    let padding_row = |cb: f64, cu: f64| -> String {
        format!("{:>11}  {:>10}  {:>9}  {:>10.4}  {:>9.0}\n", "---", "---", "---", cb, cu)
    };

    for i in (0..DEPTH).rev() {
        if i < asks.len() {
            let level = &asks[i];
            let usdt = level.price * level.volume;
            cum_btc_ask += level.volume;
            cum_usdt_ask += usdt;
            output.push_str(&format!(
                "\x1b[91m{:>11.2}\x1b[0m  {:>10.7}  {:>9.0}  {:>10.4}  {:>9.0}\n",
                level.price, level.volume, usdt, cum_btc_ask, cum_usdt_ask
            ));
        } else {
            output.push_str(&padding_row(cum_btc_ask, cum_usdt_ask));
        }
    }

    output.push_str(&format!("\n{:^70}\n", "BIDS (Buy Orders)"));
    output.push_str(&format!("{:>11}  {:>10}  {:>9}  {:>10}  {:>9}\n", "Price", "BTC", "USDT", "Cum BTC", "Cum USDT"));
    output.push_str(&format!("{div}\n"));

    let mut cum_btc_bid = 0.0;
    let mut cum_usdt_bid = 0.0;

    for i in 0..DEPTH {
        if i < bids.len() {
            let level = &bids[i];
            let usdt = level.price * level.volume;
            cum_btc_bid += level.volume;
            cum_usdt_bid += usdt;
            output.push_str(&format!(
                "\x1b[92m{:>11.2}\x1b[0m  {:>10.7}  {:>9.0}  {:>10.4}  {:>9.0}\n",
                level.price, level.volume, usdt, cum_btc_bid, cum_usdt_bid
            ));
        } else {
            output.push_str(&padding_row(cum_btc_bid, cum_usdt_bid));
        }
    }

    output.push_str(&format!("{sep}\n"));
    output
}

fn main() {
    let running = Arc::new(AtomicBool::new(true));
    let counter = Arc::new(AtomicU64::new(0));
    let r = running.clone();
    
    ctrlc::set_handler(move || {
        r.store(false, Ordering::SeqCst);
    })
    .expect("Error setting Ctrl-C handler");

    let orderbook: OrderBookState = Arc::new(Mutex::new(CoinbaseBook::new()));
    let ob_clone = orderbook.clone();
    let running_clone = running.clone();
    let counter_clone = counter.clone();

    // WebSocket thread
    thread::spawn(move || {
        while running_clone.load(Ordering::SeqCst) {
            match connect("wss://ws-feed.exchange.coinbase.com") {
                Ok((mut socket, _)) => {
                    let subscribe = serde_json::json!({
                        "type": "subscribe",
                        "product_ids": ["BTC-USD"],
                        "channels": ["level2_batch"]
                    });

                    if socket.send(WsMessage::Text(subscribe.to_string())).is_ok() {
                        println!("✓ Coinbase connected");

                        while running_clone.load(Ordering::SeqCst) {
                            match socket.read() {
                                Ok(msg) => {
                                    if let WsMessage::Text(text) = msg {
                                        if let Ok(mut book) = ob_clone.lock() {
                                            book.process_message(&text);
                                            counter_clone.fetch_add(1, Ordering::Relaxed);
                                        }
                                    }
                                }
                                Err(_) => break,
                            }
                        }
                    }
                }
                Err(e) => {
                    if running_clone.load(Ordering::SeqCst) {
                        eprintln!("Coinbase connection error: {}, reconnecting...", e);
                        thread::sleep(StdDuration::from_secs(2));
                    }
                }
            }
        }
    });

    // Display loop at 20 FPS (50ms refresh)
    thread::sleep(StdDuration::from_millis(500));
    let mut last = 0u64;
    let mut timer = Instant::now();
    let mut ups = 0.0;
    
    while running.load(Ordering::SeqCst) {
        if let Ok(book) = orderbook.lock() {
            let bids = book.get_bids(DEPTH);
            let asks = book.get_asks(DEPTH);

            if !bids.is_empty() && !asks.is_empty() {
                let curr = counter.load(Ordering::Relaxed);
                let elapsed = timer.elapsed().as_secs_f64();
                
                if elapsed >= 1.0 {
                    ups = (curr - last) as f64 / elapsed;
                    last = curr;
                    timer = Instant::now();
                }
                
                print!("{}", render_orderbook(&bids, &asks, ups, curr));
                std::io::stdout().flush().ok();
            }
        }
        thread::sleep(StdDuration::from_millis(50));
    }

    println!("\n✓ Stopped");
}
