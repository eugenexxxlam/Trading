//! BingX BTCUSDT orderbook - WebSocket live display at 20 FPS

use std::{
    io::{Read, Write},
    sync::{atomic::{AtomicBool, AtomicU64, Ordering}, Arc, Mutex},
    thread,
    time::{Duration, Instant},
};
use chrono::{FixedOffset, Utc};
use flate2::read::GzDecoder;
use serde::Deserialize;
use tungstenite::{connect, Message};

type Book = (Vec<(f64, f64)>, Vec<(f64, f64)>);

#[derive(Deserialize)]
struct BingxResponse {
    ping: Option<i64>,
    data: Option<BookData>,
}

#[derive(Deserialize)]
struct BookData {
    bids: Vec<Vec<String>>,
    asks: Vec<Vec<String>>,
}

fn parse_depth(json: &str) -> Option<Book> {
    let resp: BingxResponse = serde_json::from_str(json).ok()?;
    
    // Skip ping messages (handled separately)
    if resp.ping.is_some() {
        return None;
    }
    
    let book_data = resp.data?;
    
    let parse = |levels: &[Vec<String>]| -> Vec<(f64, f64)> {
        levels.iter()
            .filter_map(|v| {
                if v.len() >= 2 {
                    Some((v[0].parse().ok()?, v[1].parse().ok()?))
                } else {
                    None
                }
            })
            .collect()
    };
    
    let (mut bids, mut asks) = (parse(&book_data.bids), parse(&book_data.asks));
    bids.sort_by(|a, b| b.0.partial_cmp(&a.0).unwrap());
    asks.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap());
    
    // Sanitize crossed levels
    if !bids.is_empty() && !asks.is_empty() {
        let (best_bid, best_ask) = (bids[0].0, asks[0].0);
        bids.retain(|&(p, _)| p < best_ask);
        asks.retain(|&(p, _)| p > best_bid);
    }
    
    if !bids.is_empty() && !asks.is_empty() {
        Some((bids, asks))
    } else {
        None
    }
}

fn decompress_gzip(data: &[u8]) -> Result<String, std::io::Error> {
    let mut decoder = GzDecoder::new(data);
    let mut decompressed = String::new();
    decoder.read_to_string(&mut decompressed)?;
    Ok(decompressed)
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
        "BingX BTC-USDT Orderbook (Rust)",
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
    let (running, book, counter) = (
        Arc::new(AtomicBool::new(true)),
        Arc::new(Mutex::new((vec![], vec![]))),
        Arc::new(AtomicU64::new(0))
    );
    
    ctrlc::set_handler({
        let r = Arc::clone(&running);
        move || r.store(false, Ordering::SeqCst)
    }).expect("Failed to set Ctrl+C handler");
    
    thread::spawn({
        let (r, b, c) = (Arc::clone(&running), Arc::clone(&book), Arc::clone(&counter));
        move || {
            const URL: &str = "wss://open-api-ws.bingx.com/market";
            while r.load(Ordering::SeqCst) {
                if let Ok((mut ws, _)) = connect(URL) {
                    // Subscribe to order book
                    let sub = r#"{"id":"bingx-rust-orderbook","reqType":"sub","dataType":"BTC-USDT@depth20"}"#;
                    if ws.send(Message::Text(sub.to_string())).is_ok() {
                        println!("✓ BingX connected");
                    }
                    
                    while r.load(Ordering::SeqCst) {
                        match ws.read() {
                            Ok(Message::Binary(data)) => {
                                // Try to decompress gzip
                                let text = decompress_gzip(&data)
                                    .unwrap_or_else(|_| String::from_utf8_lossy(&data).to_string());
                                
                                // Handle ping/pong
                                if text.contains("\"ping\"") {
                                    if let Ok(resp) = serde_json::from_str::<BingxResponse>(&text) {
                                        if let Some(ping_val) = resp.ping {
                                            let pong = format!(r#"{{"pong":{}}}"#, ping_val);
                                            let _ = ws.send(Message::Text(pong));
                                            continue;
                                        }
                                    }
                                }
                                
                                // Parse orderbook data
                                if let Some(bk) = parse_depth(&text) {
                                    *b.lock().unwrap() = bk;
                                    c.fetch_add(1, Ordering::Relaxed);
                                }
                            },
                            Ok(Message::Text(t)) => {
                                // Handle ping/pong
                                if t.contains("\"ping\"") {
                                    if let Ok(resp) = serde_json::from_str::<BingxResponse>(&t) {
                                        if let Some(ping_val) = resp.ping {
                                            let pong = format!(r#"{{"pong":{}}}"#, ping_val);
                                            let _ = ws.send(Message::Text(pong));
                                            continue;
                                        }
                                    }
                                }
                                
                                // Parse orderbook data
                                if let Some(bk) = parse_depth(&t) {
                                    *b.lock().unwrap() = bk;
                                    c.fetch_add(1, Ordering::Relaxed);
                                }
                            },
                            Ok(Message::Ping(d)) => { let _ = ws.send(Message::Pong(d)); },
                            Ok(Message::Close(_)) | Err(_) => break,
                            _ => {}
                        }
                    }
                }
                if r.load(Ordering::SeqCst) {
                    println!("BingX error, reconnecting...");
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
