# UML Diagrams for Low-Latency Trading System

This document contains comprehensive UML diagrams for the low-latency trading system.

## Table of Contents
1. [System Architecture](#1-system-architecture)
2. [Common Module Classes](#2-common-module-classes)
3. [Exchange Module Classes](#3-exchange-module-classes)
4. [Trading Module Classes](#4-trading-module-classes)
5. [Order Submission Flow](#5-order-submission-flow-sequence)
6. [Market Data Flow](#6-market-data-flow-sequence)
7. [Order Book Data Structures](#7-order-book-data-structures)

---

## 1. System Architecture

High-level component diagram showing the main modules and their interactions.

```plantuml
@startuml
!theme plain

package "Common Infrastructure" {
  [LFQueue] as LFQ
  [MemPool] as MP
  [Logger] as LOG
  [TCPSocket] as TCP
  [McastSocket] as MCAST
}

package "Exchange Side" {
  component "Order Server" as OS {
    [TCP Receiver]
    [FIFO Sequencer]
  }
  
  component "Matching Engine" as ME {
    [Order Book AAPL]
    [Order Book MSFT]
    [Order Book ...]
  }
  
  component "Market Data Publisher" as MDP {
    [Incremental Stream]
    [Snapshot Synthesizer]
  }
}

package "Trading Client Side" {
  component "Order Gateway" as OGW {
    [TCP Sender]
  }
  
  component "Market Data Consumer" as MDC {
    [Incremental Receiver]
    [Snapshot Receiver]
  }
  
  component "Trade Engine" as TE {
    [Market Order Books]
    [Feature Engine]
    [Position Keeper]
    [Risk Manager]
    [Order Manager]
  }
  
  component "Trading Strategies" as STRAT {
    [Market Maker]
    [Liquidity Taker]
  }
}

' Connections
[Trading Strategies] --> [Order Manager] : Generate Orders
[Order Manager] --> [Order Gateway] : via LFQueue
[Order Gateway] --> [Order Server] : TCP
[Order Server] --> [Matching Engine] : via LFQueue
[Matching Engine] --> [Order Server] : Responses\n(via LFQueue)
[Order Server] --> [Order Gateway] : TCP
[Order Gateway] --> [Trade Engine] : via LFQueue

[Matching Engine] --> [Market Data Publisher] : Updates\n(via LFQueue)
[Market Data Publisher] --> [Market Data Consumer] : UDP Multicast
[Market Data Consumer] --> [Trade Engine] : via LFQueue
[Trade Engine] --> [Trading Strategies] : Callbacks

note right of ME
  Single-threaded per instrument
  Lock-free queues
  Memory pools
  <1 μs latency
end note

note right of MDP
  UDP Multicast
  Sequence numbers
  Snapshot + Incremental
end note

note right of TE
  Order book reconstruction
  Feature computation
  Risk checks
  Position tracking
end note

@enduml
```

---

## 2. Common Module Classes

Core infrastructure classes used throughout the system.

```plantuml
@startuml
!theme plain

namespace Common {
  ' Core Types
  class "«typedef» OrderId" as OrderId {
    uint64_t
  }
  
  class "«typedef» TickerId" as TickerId {
    uint32_t
  }
  
  class "«typedef» ClientId" as ClientId {
    uint32_t
  }
  
  class "«typedef» Price" as Price {
    int64_t (fixed-point)
  }
  
  class "«typedef» Qty" as Qty {
    uint32_t
  }
  
  enum Side {
    INVALID = 0
    BUY = 1
    SELL = -1
    MAX = 2
  }
  
  enum AlgoType {
    INVALID
    RANDOM
    MAKER
    TAKER
  }
  
  ' Lock-Free Queue
  class LFQueue<T> {
    - store_: vector<T>
    - next_write_index_: atomic<size_t>
    - next_read_index_: atomic<size_t>
    - num_elements_: atomic<size_t>
    --
    + LFQueue(num_elems: size_t)
    + getNextToWriteTo(): T*
    + updateWriteIndex(): void
    + getNextToRead(): const T*
    + updateReadIndex(): void
    + size(): size_t
  }
  
  ' Memory Pool
  class MemPool<T> {
    - store_: vector<ObjectBlock>
    - next_free_index_: size_t
    --
    + MemPool(num_elems: size_t)
    + allocate(args...): T*
    + deallocate(elem: const T*): void
  }
  
  class ObjectBlock<T> {
    + object_: T
    + is_free_: bool
  }
  
  ' Logger
  class Logger {
    - file_name_: string
    - file_: ofstream
    - queue_: LFQueue<LogElement>
    - running_: atomic<bool>
    - logger_thread_: thread*
    --
    + Logger(file_name: string)
    + ~Logger()
    + log(format: const char*, args...): void
    - flushQueue(): void
  }
  
  enum LogType {
    CHAR
    INTEGER
    LONG_INTEGER
    UNSIGNED_INTEGER
    FLOAT
    DOUBLE
  }
  
  struct LogElement {
    + type_: LogType
    + u_: union
  }
  
  ' TCP Socket
  class TCPSocket {
    + socket_fd_: int
    + outbound_data_: vector<char>
    + next_send_valid_index_: size_t
    + inbound_data_: vector<char>
    + next_rcv_valid_index_: size_t
    + socket_attrib_: sockaddr_in
    + recv_callback_: function<void(TCPSocket*, Nanos)>
    --
    + TCPSocket(logger: Logger&)
    + connect(ip, iface, port, is_listening): int
    + sendAndRecv(): bool
    + send(data, len): void
  }
  
  class TCPServer {
    + epoll_fd_: int
    + listener_socket_: TCPSocket
    + events_: epoll_event[1024]
    + receive_sockets_: vector<TCPSocket*>
    + send_sockets_: vector<TCPSocket*>
    + recv_callback_: function<void(TCPSocket*, Nanos)>
    + recv_finished_callback_: function<void()>
    --
    + TCPServer(logger: Logger&)
    + listen(iface, port): void
    + poll(): void
    + sendAndRecv(): void
  }
  
  ' Multicast Socket
  class McastSocket {
    + socket_fd_: int
    + outbound_data_: vector<char>
    + next_send_valid_index_: size_t
    + inbound_data_: vector<char>
    + next_rcv_valid_index_: size_t
    + recv_callback_: function<void(McastSocket*)>
    --
    + McastSocket(logger: Logger&)
    + init(ip, iface, port, is_listening): int
    + join(ip): bool
    + leave(ip, port): void
    + sendAndRecv(): bool
    + send(data, len): void
  }
  
  ' Relationships
  MemPool *-- ObjectBlock : contains
  Logger o-- LogElement : queues
  Logger o-- LFQueue : uses
  TCPServer *-- TCPSocket : manages
}

note bottom of LFQueue
  SPSC (Single Producer Single Consumer)
  Lock-free circular buffer
  10-20 ns enqueue/dequeue
  Used for inter-thread communication
end note

note bottom of MemPool
  Pre-allocated object pool
  O(1) allocate/deallocate
  No heap fragmentation
  Deterministic latency
end note

@enduml
```

---

## 3. Exchange Module Classes

Exchange-side components: Order Server, Matching Engine, Market Data Publisher.

```plantuml
@startuml
!theme plain

namespace Exchange {
  ' Client Request/Response
  enum ClientRequestType {
    INVALID
    NEW
    CANCEL
  }
  
  enum ClientResponseType {
    INVALID
    ACCEPTED
    CANCELED
    FILLED
    CANCEL_REJECTED
  }
  
  class MEClientRequest <<packed>> {
    + type_: ClientRequestType
    + client_id_: ClientId
    + ticker_id_: TickerId
    + order_id_: OrderId
    + side_: Side
    + price_: Price
    + qty_: Qty
    --
    + toString(): string
  }
  
  class OMClientRequest <<packed>> {
    + seq_num_: size_t
    + me_client_request_: MEClientRequest
    --
    + toString(): string
  }
  
  class MEClientResponse <<packed>> {
    + type_: ClientResponseType
    + client_id_: ClientId
    + ticker_id_: TickerId
    + client_order_id_: OrderId
    + market_order_id_: OrderId
    + side_: Side
    + price_: Price
    + exec_qty_: Qty
    + leaves_qty_: Qty
    --
    + toString(): string
  }
  
  class OMClientResponse <<packed>> {
    + seq_num_: size_t
    + me_client_response_: MEClientResponse
    --
    + toString(): string
  }
  
  ' Market Data
  enum MarketUpdateType {
    INVALID
    CLEAR
    ADD
    MODIFY
    CANCEL
    TRADE
    SNAPSHOT_START
    SNAPSHOT_END
  }
  
  class MEMarketUpdate <<packed>> {
    + type_: MarketUpdateType
    + order_id_: OrderId
    + ticker_id_: TickerId
    + side_: Side
    + price_: Price
    + qty_: Qty
    + priority_: Priority
    --
    + toString(): string
  }
  
  class MDPMarketUpdate <<packed>> {
    + seq_num_: size_t
    + me_market_update_: MEMarketUpdate
    --
    + toString(): string
  }
  
  ' Order Server
  class OrderServer {
    - iface_: string
    - port_: int
    - outgoing_responses_: ClientResponseLFQueue*
    - run_: volatile bool
    - tcp_server_: TCPServer
    - fifo_sequencer_: FIFOSequencer
    - cid_next_outgoing_seq_num_: array<size_t>
    - cid_next_exp_seq_num_: array<size_t>
    - cid_tcp_socket_: array<TCPSocket*>
    --
    + OrderServer(requests, responses, iface, port)
    + start(): void
    + stop(): void
    + run(): void {noexcept}
    - recvCallback(socket, rx_time): void {noexcept}
    - recvFinishedCallback(): void {noexcept}
  }
  
  class FIFOSequencer {
    - pending_client_requests_: vector<RecvTimeClientRequest>
    - client_request_queue_: ClientRequestLFQueue*
    --
    + FIFOSequencer(queue)
    + addClientRequest(rx_time, request): void
    + sequenceAndPublish(): void
  }
  
  ' Matching Engine
  class MatchingEngine {
    - ticker_order_book_: OrderBookHashMap
    - incoming_requests_: ClientRequestLFQueue*
    - outgoing_ogw_responses_: ClientResponseLFQueue*
    - outgoing_md_updates_: MEMarketUpdateLFQueue*
    - run_: volatile bool
    - next_market_order_id_: OrderId
    --
    + MatchingEngine(requests, responses, updates)
    + start(): void
    + stop(): void
    + run(): void {noexcept}
    + processClientRequest(request): void {noexcept}
    + sendClientResponse(response): void {noexcept}
    + sendMarketUpdate(update): void {noexcept}
  }
  
  class MEOrderBook {
    - ticker_id_: TickerId
    - matching_engine_: MatchingEngine*
    - cid_oid_to_order_: ClientOrderHashMap
    - orders_at_price_pool_: MemPool<MEOrdersAtPrice>
    - bids_by_price_: MEOrdersAtPrice*
    - asks_by_price_: MEOrdersAtPrice*
    - price_orders_at_price_: OrdersAtPriceHashMap
    - order_pool_: MemPool<MEOrder>
    - client_response_: MEClientResponse
    - market_update_: MEMarketUpdate
    --
    + MEOrderBook(ticker_id, logger, engine)
    + add(client_id, order_id, ticker_id, side, price, qty): void {noexcept}
    + cancel(client_id, order_id, ticker_id): void {noexcept}
    + toString(detailed, validity_check): string
    - checkForMatch(...): void
    - match(...): void
    - addOrder(order): void
    - removeOrder(order): void
    - addOrdersAtPrice(orders): void
    - removeOrdersAtPrice(side, price): void
    - getOrdersAtPrice(price): MEOrdersAtPrice*
    - getNextPriority(price): Priority
  }
  
  struct MEOrder {
    + ticker_id_: TickerId
    + client_id_: ClientId
    + client_order_id_: OrderId
    + market_order_id_: OrderId
    + side_: Side
    + price_: Price
    + qty_: Qty
    + priority_: Priority
    + prev_order_: MEOrder*
    + next_order_: MEOrder*
    --
    + MEOrder(...)
    + toString(): string
  }
  
  struct MEOrdersAtPrice {
    + side_: Side
    + price_: Price
    + first_me_order_: MEOrder*
    + prev_entry_: MEOrdersAtPrice*
    + next_entry_: MEOrdersAtPrice*
    --
    + MEOrdersAtPrice(...)
    + toString(): string
  }
  
  ' Market Data Publisher
  class MarketDataPublisher {
    - next_inc_seq_num_: size_t
    - outgoing_md_updates_: MEMarketUpdateLFQueue*
    - snapshot_md_updates_: MDPMarketUpdateLFQueue
    - run_: volatile bool
    - incremental_socket_: McastSocket
    - snapshot_synthesizer_: SnapshotSynthesizer*
    --
    + MarketDataPublisher(updates, iface, snapshot_ip, snapshot_port, incremental_ip, incremental_port)
    + start(): void
    + stop(): void
    + run(): void {noexcept}
  }
  
  class SnapshotSynthesizer {
    - ticker_order_book_: OrderBookHashMap
    - run_: volatile bool
    - incremental_mcast_socket_: McastSocket
    --
    + SnapshotSynthesizer(queue, iface, ip, port)
    + start(): void
    + stop(): void
    + run(): void {noexcept}
  }
  
  ' Relationships
  OMClientRequest *-- MEClientRequest : contains
  OMClientResponse *-- MEClientResponse : contains
  MDPMarketUpdate *-- MEMarketUpdate : contains
  
  OrderServer o-- FIFOSequencer : uses
  OrderServer o-- Common.TCPServer : uses
  
  MatchingEngine *-- MEOrderBook : manages
  MEOrderBook *-- MEOrder : contains
  MEOrderBook *-- MEOrdersAtPrice : contains
  MEOrder --> MEOrder : doubly linked
  MEOrdersAtPrice --> MEOrdersAtPrice : doubly linked
  MEOrdersAtPrice --> MEOrder : points to first
  
  MarketDataPublisher o-- SnapshotSynthesizer : manages
  MarketDataPublisher o-- Common.McastSocket : uses
  SnapshotSynthesizer o-- Common.McastSocket : uses
}

note top of MEOrderBook
  Time-Price Priority Matching
  Doubly linked lists for FIFO
  Memory pools (no heap)
  O(1) add/cancel/match
  ~20-50 ns per operation
end note

note top of MarketDataPublisher
  UDP Multicast streams:
  - Incremental (real-time)
  - Snapshot (recovery)
  Sequence numbers (gap detection)
end note

@enduml
```

---

## 4. Trading Module Classes

Trading client components: Order Gateway, Market Data Consumer, Trade Engine, Strategies.

```plantuml
@startuml
!theme plain

namespace Trading {
  ' Order Gateway
  class OrderGateway {
    - client_id_: ClientId
    - ip_: string
    - iface_: string
    - port_: int
    - outgoing_requests_: ClientRequestLFQueue*
    - incoming_responses_: ClientResponseLFQueue*
    - run_: volatile bool
    - next_outgoing_seq_num_: size_t
    - next_exp_seq_num_: size_t
    - tcp_socket_: TCPSocket
    --
    + OrderGateway(client_id, requests, responses, ip, iface, port)
    + start(): void
    + stop(): void
    - run(): void {noexcept}
    - recvCallback(socket, rx_time): void {noexcept}
  }
  
  ' Market Data Consumer
  class MarketDataConsumer {
    - next_exp_inc_seq_num_: size_t
    - incoming_md_updates_: MEMarketUpdateLFQueue*
    - run_: volatile bool
    - incremental_mcast_socket_: McastSocket
    - snapshot_mcast_socket_: McastSocket
    - in_recovery_: bool
    - snapshot_queued_msgs_: QueuedMarketUpdates
    - incremental_queued_msgs_: QueuedMarketUpdates
    --
    + MarketDataConsumer(client_id, updates, iface, snapshot_ip, snapshot_port, incremental_ip, incremental_port)
    + start(): void
    + stop(): void
    - run(): void {noexcept}
    - recvCallback(socket): void {noexcept}
    - queueMessage(is_snapshot, request): void
    - startSnapshotSync(): void
    - checkSnapshotSync(): void
  }
  
  ' Market Order Book (Client-side)
  class MarketOrderBook {
    - ticker_id_: TickerId
    - trade_engine_: TradeEngine*
    - oid_to_order_: OrderHashMap
    - orders_at_price_pool_: MemPool<MarketOrdersAtPrice>
    - bids_by_price_: MarketOrdersAtPrice*
    - asks_by_price_: MarketOrdersAtPrice*
    - price_orders_at_price_: OrdersAtPriceHashMap
    - order_pool_: MemPool<MarketOrder>
    - bbo_: BBO
    --
    + MarketOrderBook(ticker_id, logger)
    + onMarketUpdate(update): void {noexcept}
    + setTradeEngine(engine): void
    + updateBBO(update_bid, update_ask): void {noexcept}
    + getBBO(): const BBO*
    + toString(detailed, validity_check): string
    - addOrder(order): void
    - removeOrder(order): void
    - addOrdersAtPrice(orders): void
    - removeOrdersAtPrice(side, price): void
    - getOrdersAtPrice(price): MarketOrdersAtPrice*
  }
  
  struct MarketOrder {
    + order_id_: OrderId
    + side_: Side
    + price_: Price
    + qty_: Qty
    + priority_: Priority
    + prev_order_: MarketOrder*
    + next_order_: MarketOrder*
    --
    + MarketOrder(...)
    + toString(): string
  }
  
  struct MarketOrdersAtPrice {
    + side_: Side
    + price_: Price
    + first_mkt_order_: MarketOrder*
    + prev_entry_: MarketOrdersAtPrice*
    + next_entry_: MarketOrdersAtPrice*
    --
    + MarketOrdersAtPrice(...)
    + toString(): string
  }
  
  struct BBO {
    + bid_price_: Price
    + ask_price_: Price
    + bid_qty_: Qty
    + ask_qty_: Qty
    --
    + toString(): string
  }
  
  ' Trade Engine
  class TradeEngine {
    - client_id_: ClientId
    - ticker_order_book_: MarketOrderBookHashMap
    - outgoing_ogw_requests_: ClientRequestLFQueue*
    - incoming_ogw_responses_: ClientResponseLFQueue*
    - incoming_md_updates_: MEMarketUpdateLFQueue*
    - run_: volatile bool
    - feature_engine_: FeatureEngine
    - position_keeper_: PositionKeeper
    - order_manager_: OrderManager
    - risk_manager_: RiskManager
    - mm_algo_: MarketMaker*
    - taker_algo_: LiquidityTaker*
    --
    + TradeEngine(client_id, algo_type, ticker_cfg, requests, responses, updates)
    + start(): void
    + stop(): void
    + run(): void {noexcept}
    + sendClientRequest(request): void {noexcept}
    + onOrderBookUpdate(ticker_id, price, side, book): void {noexcept}
    + onTradeUpdate(update, book): void {noexcept}
    + onOrderUpdate(response): void {noexcept}
  }
  
  ' Feature Engine
  class FeatureEngine {
    - mkt_price_: double
    - agg_trade_qty_ratio_: double
    --
    + FeatureEngine(logger)
    + onOrderBookUpdate(ticker_id, price, side, book): void {noexcept}
    + onTradeUpdate(update, book): void {noexcept}
    + getMktPrice(): double
    + getAggTradeQtyRatio(): double
  }
  
  ' Position Keeper
  class PositionKeeper {
    - ticker_position_: array<PositionInfo>
    --
    + PositionKeeper(logger)
    + addFill(response): void {noexcept}
    + updateBBO(ticker_id, bbo): void {noexcept}
    + getPositionInfo(ticker_id): const PositionInfo*
    + toString(): string
  }
  
  struct PositionInfo {
    + position_: int32_t
    + real_pnl_: double
    + unreal_pnl_: double
    + total_pnl_: double
    + open_vwap_: array<double>
    + volume_: Qty
    + bbo_: const BBO*
    --
    + addFill(response, logger): void {noexcept}
    + updateBBO(bbo, logger): void {noexcept}
    + toString(): string
  }
  
  ' Order Manager
  class OrderManager {
    - trade_engine_: TradeEngine*
    - risk_manager_: RiskManager&
    - ticker_side_order_: OMOrderTickerSideHashMap
    - next_order_id_: OrderId
    --
    + OrderManager(logger, engine, risk_manager)
    + onOrderUpdate(response): void {noexcept}
    + newOrder(order, ticker_id, price, side, qty): void {noexcept}
    + cancelOrder(order): void {noexcept}
    + moveOrder(order, ticker_id, price, side, qty): void {noexcept}
    + moveOrders(ticker_id, bid_price, ask_price, clip): void {noexcept}
    + getOMOrderSideHashMap(ticker_id): OMOrderSideHashMap*
  }
  
  enum OMOrderState {
    INVALID
    PENDING_NEW
    LIVE
    PENDING_CANCEL
    DEAD
  }
  
  struct OMOrder {
    + ticker_id_: TickerId
    + order_id_: OrderId
    + side_: Side
    + price_: Price
    + qty_: Qty
    + order_state_: OMOrderState
    --
    + toString(): string
  }
  
  ' Risk Manager
  class RiskManager {
    - ticker_risk_: TickerRiskInfoHashMap
    --
    + RiskManager(logger, position_keeper, ticker_cfg)
    + checkPreTradeRisk(ticker_id, side, qty): RiskCheckResult {noexcept}
  }
  
  enum RiskCheckResult {
    INVALID
    ORDER_TOO_LARGE
    POSITION_TOO_LARGE
    LOSS_TOO_LARGE
    ALLOWED
  }
  
  struct RiskInfo {
    + position_info_: const PositionInfo*
    + risk_cfg_: RiskCfg
    --
    + checkPreTradeRisk(side, qty): RiskCheckResult {noexcept}
    + toString(): string
  }
  
  struct RiskCfg {
    + max_order_size_: Qty
    + max_position_: Qty
    + max_loss_: double
    --
    + toString(): string
  }
  
  ' Trading Strategies
  class MarketMaker {
    - feature_engine_: const FeatureEngine*
    - order_manager_: OrderManager*
    - ticker_cfg_: TradeEngineCfgHashMap
    --
    + MarketMaker(logger, engine, feature_engine, order_manager, ticker_cfg)
    + onOrderBookUpdate(ticker_id, price, side, book): void {noexcept}
    + onTradeUpdate(update, book): void {noexcept}
    + onOrderUpdate(response): void {noexcept}
  }
  
  class LiquidityTaker {
    - feature_engine_: const FeatureEngine*
    - order_manager_: OrderManager*
    - ticker_cfg_: TradeEngineCfgHashMap
    --
    + LiquidityTaker(logger, engine, feature_engine, order_manager, ticker_cfg)
    + onOrderBookUpdate(ticker_id, price, side, book): void {noexcept}
    + onTradeUpdate(update, book): void {noexcept}
    + onOrderUpdate(response): void {noexcept}
  }
  
  ' Relationships
  OrderGateway o-- Common.TCPSocket : uses
  MarketDataConsumer o-- Common.McastSocket : uses
  
  MarketOrderBook *-- MarketOrder : contains
  MarketOrderBook *-- MarketOrdersAtPrice : contains
  MarketOrderBook *-- BBO : maintains
  MarketOrder --> MarketOrder : doubly linked
  MarketOrdersAtPrice --> MarketOrdersAtPrice : doubly linked
  MarketOrdersAtPrice --> MarketOrder : points to first
  
  TradeEngine *-- MarketOrderBook : manages
  TradeEngine *-- FeatureEngine : uses
  TradeEngine *-- PositionKeeper : uses
  TradeEngine *-- OrderManager : uses
  TradeEngine *-- RiskManager : uses
  TradeEngine o-- MarketMaker : strategy
  TradeEngine o-- LiquidityTaker : strategy
  
  PositionKeeper *-- PositionInfo : contains
  PositionInfo --> BBO : references
  
  OrderManager o-- OMOrder : manages
  OrderManager --> RiskManager : checks
  
  RiskManager *-- RiskInfo : contains
  RiskInfo --> PositionInfo : references
  RiskInfo *-- RiskCfg : config
  
  MarketMaker --> FeatureEngine : uses
  MarketMaker --> OrderManager : sends orders
  LiquidityTaker --> FeatureEngine : uses
  LiquidityTaker --> OrderManager : sends orders
}

note top of TradeEngine
  Central trading hub:
  - Consumes market data
  - Sends orders
  - Tracks position/PnL
  - Risk management
  - Strategy execution
end note

note top of MarketMaker
  Passive liquidity provider:
  - Post bids and asks
  - Earn bid-ask spread
  - Quote around fair price
end note

note top of LiquidityTaker
  Aggressive taker:
  - Follow momentum
  - Cross the spread
  - Take available liquidity
end note

@enduml
```

---

## 5. Order Submission Flow (Sequence)

Sequence diagram showing order flow from trading strategy to execution report.

```plantuml
@startuml
!theme plain

actor "Market Maker\nStrategy" as MM
participant "Order Manager" as OM
participant "Risk Manager" as RM
participant "Trade Engine" as TE
participant "Order Gateway" as OGW
queue "LF Queue\n(Requests)" as Q1
participant "Order Server" as OS
participant "FIFO Sequencer" as FS
queue "LF Queue\n(To ME)" as Q2
participant "Matching\nEngine" as ME
participant "Order Book\n(AAPL)" as OB
queue "LF Queue\n(Responses)" as Q3
participant "Order Gateway" as OGW2
participant "Trade Engine" as TE2
participant "Order Manager" as OM2
participant "Market Maker\nStrategy" as MM2

== Order Submission ==

MM -> OM: moveOrders(ticker, bid, ask, qty)
activate OM

OM -> RM: checkPreTradeRisk(ticker, BUY, qty)
activate RM
RM --> OM: ALLOWED
deactivate RM

OM -> OM: newOrder(order, ticker, price, BUY, qty)
OM -> TE: sendClientRequest(request)
deactivate OM
activate TE

TE -> Q1: Write MEClientRequest
note right
  T10: Trade engine writes to queue
  Lock-free queue (10-20 ns)
end note
deactivate TE

Q1 -> OGW: Read MEClientRequest
activate OGW
note right
  T11: Order gateway reads from queue
end note

OGW -> OGW: Add sequence number
OGW -> OS: Send OMClientRequest\n(TCP, seq#1234)
note right
  T12: TCP send (1-5 μs)
end note
deactivate OGW

== Exchange Processing ==

OS -> OS: Receive OMClientRequest
activate OS
note right
  T1: Order server TCP receive
  Validate client ID, sequence number
end note

OS -> FS: addClientRequest(rx_time, request)
deactivate OS
activate FS

FS -> FS: Buffer with timestamp
FS -> FS: sequenceAndPublish()\n(Time-priority sort)
FS -> Q2: Write MEClientRequest
note right
  T2: FIFO sequencer publishes
end note
deactivate FS

Q2 -> ME: Read MEClientRequest
activate ME
note right
  T3: Matching engine reads
end note

ME -> OB: add(client_id, order_id, ticker, BUY, price, qty)
activate OB

OB -> OB: generateNewMarketOrderId()
OB -> OB: checkForMatch()\n(Try to match with asks)

alt Order Matches
  OB -> OB: match()\n(Execute against passive orders)
  OB -> ME: sendClientResponse(FILLED)
  OB -> ME: sendMarketUpdate(TRADE)
else Order Rests
  OB -> OB: addOrder()\n(Add to bid book, FIFO queue)
  OB -> ME: sendClientResponse(ACCEPTED)
  OB -> ME: sendMarketUpdate(ADD)
end
deactivate OB

ME -> Q3: Write MEClientResponse
note right
  T4t: Matching engine to order server queue
end note
deactivate ME

== Response ==

Q3 -> OS: Read MEClientResponse
activate OS
note right
  T5t: Order server reads
end note

OS -> OS: Add sequence number
OS -> OGW2: Send OMClientResponse\n(TCP, seq#567)
note right
  T6t: TCP send
end note
deactivate OS

OGW2 -> OGW2: Receive OMClientResponse
activate OGW2
note right
  T7t: Order gateway receives
  Validate sequence number
end note

OGW2 -> Q1: Write MEClientResponse
note right
  T8t: Write to queue
end note
deactivate OGW2

Q1 -> TE2: Read MEClientResponse
activate TE2
note right
  T9t: Trade engine reads
end note

TE2 -> OM2: onOrderUpdate(response)
activate OM2
OM2 -> OM2: Update order state\n(PENDING_NEW -> LIVE)
deactivate OM2

alt Response Type = FILLED
  TE2 -> "Position\nKeeper": addFill(response)
  note right: Update position, PnL, VWAP
end

TE2 -> MM2: onOrderUpdate(response)
deactivate TE2
activate MM2
MM2 -> MM2: Log execution
deactivate MM2

note over MM, MM2
  **Total Latency**: ~5-20 μs (co-located)
  - Order gateway: 1-5 μs
  - Network: 0.1-1 μs
  - Exchange: 1-5 μs
  - Network: 0.1-1 μs
  - Order gateway: 1-3 μs
end note

@enduml
```

---

## 6. Market Data Flow (Sequence)

Sequence diagram showing market data flow from exchange to trading strategy.

```plantuml
@startuml
!theme plain

participant "Matching\nEngine" as ME
queue "LF Queue\n(MD Updates)" as Q1
participant "Market Data\nPublisher" as MDP
participant "Multicast\nNetwork\n(UDP)" as NET
participant "Market Data\nConsumer" as MDC
queue "LF Queue\n(MD Updates)" as Q2
participant "Trade Engine" as TE
participant "Market\nOrder Book" as MOB
participant "Feature\nEngine" as FE
participant "Market Maker\nStrategy" as MM
participant "Order\nManager" as OM

== Market Data Update ==

ME -> ME: Order book change\n(NEW/CANCEL/MODIFY/TRADE)
activate ME

ME -> Q1: Write MEMarketUpdate
note right
  T4: Matching engine publishes
  Update type: ADD/CANCEL/MODIFY/TRADE
end note
deactivate ME

Q1 -> MDP: Read MEMarketUpdate
activate MDP
note right
  Matching engine -> Publisher
  Lock-free queue (10-20 ns)
end note

MDP -> MDP: Add sequence number\n(seq#12345)
MDP -> MDP: Create MDPMarketUpdate

MDP -> NET: UDP Multicast Send\n(239.0.0.1:12345)
note right
  Incremental feed
  1-10 μs network latency
  All subscribers receive simultaneously
end note
deactivate MDP

== Market Data Consumption ==

NET -> MDC: UDP Receive MDPMarketUpdate
activate MDC
note right
  Non-blocking UDP receive
  Sequence number validation
end note

MDC -> MDC: Validate sequence\n(detect gaps)

alt Sequence Gap Detected
  MDC -> MDC: Enter recovery mode
  MDC -> NET: Subscribe to snapshot feed
  MDC -> MDC: Buffer incremental updates
  MDC -> MDC: Wait for complete snapshot
  MDC -> MDC: Sync and exit recovery
else Normal Operation
  MDC -> Q2: Write MEMarketUpdate
  note right
    Forward to trade engine
  end note
end
deactivate MDC

Q2 -> TE: Read MEMarketUpdate
activate TE
note right
  Trade engine processes update
  Reconstruct order book
end note

TE -> MOB: onMarketUpdate(update)
activate MOB

alt Update Type = ADD
  MOB -> MOB: addOrder()\n(Add to book)
  MOB -> MOB: updateBBO()\n(Update best bid/offer)
else Update Type = MODIFY
  MOB -> MOB: Update quantity
  MOB -> MOB: updateBBO()
else Update Type = CANCEL
  MOB -> MOB: removeOrder()
  MOB -> MOB: updateBBO()
else Update Type = TRADE
  note right: Trade notification\n(no book change)
end
deactivate MOB

alt Order Book Change
  TE -> FE: onOrderBookUpdate(ticker, price, side, book)
  activate FE
  FE -> MOB: getBBO()
  FE -> FE: Compute weighted mid price\n(fair price)
  note right
    fair = (bid*ask_qty + ask*bid_qty) / (bid_qty + ask_qty)
  end note
  FE --> TE: Feature updated (fair price)
  deactivate FE
  
  TE -> "Position\nKeeper": updateBBO(ticker, bbo)
  note right: Update unrealized PnL
  
  TE -> MM: onOrderBookUpdate(ticker, price, side, book)
  activate MM
  MM -> FE: getMktPrice()
  MM -> MOB: getBBO()
  MM -> MM: Calculate bid/ask prices\n(around fair price)
  MM -> OM: moveOrders(ticker, bid, ask, clip)
  note right
    Market maker adjusts quotes
    Based on fair price and threshold
  end note
  deactivate MM
end

alt Trade Update
  TE -> FE: onTradeUpdate(market_update, book)
  activate FE
  FE -> MOB: getBBO()
  FE -> FE: Compute aggressive trade ratio\n(trade_qty / bbo_qty)
  FE --> TE: Feature updated (momentum)
  deactivate FE
  
  TE -> "Liquidity\nTaker": onTradeUpdate(market_update, book)
  note right
    Taker may follow momentum
    If ratio > threshold
  end note
end

deactivate TE

note over ME, OM
  **Market Data Latency**: ~1-10 μs
  - Matching engine to publisher: 0.01-0.02 μs (queue)
  - Publisher to network: 1-10 μs (UDP)
  - Network propagation: 0.1-1 μs (LAN)
  - Consumer to trade engine: 0.01-0.02 μs (queue)
  - Book reconstruction: 0.05-0.2 μs
  - Feature computation: 0.02-0.1 μs
  - Strategy decision: 0.1-1 μs
end note

@enduml
```

---

## 7. Order Book Data Structures

Detailed class diagram showing order book internal structure (doubly-linked lists).

```plantuml
@startuml
!theme plain

package "Order Book Internal Structure" {
  ' Exchange Side
  class MEOrderBook {
    - bids_by_price_: MEOrdersAtPrice*
    - asks_by_price_: MEOrdersAtPrice*
    - price_orders_at_price_: array<MEOrdersAtPrice*>
    - cid_oid_to_order_: array<array<MEOrder*>>
    - order_pool_: MemPool<MEOrder>
    - orders_at_price_pool_: MemPool<MEOrdersAtPrice>
  }
  
  class MEOrdersAtPrice {
    + side_: Side
    + price_: Price
    + first_me_order_: MEOrder*
    + prev_entry_: MEOrdersAtPrice*
    + next_entry_: MEOrdersAtPrice*
  }
  
  class MEOrder {
    + ticker_id_: TickerId
    + client_id_: ClientId
    + client_order_id_: OrderId
    + market_order_id_: OrderId
    + side_: Side
    + price_: Price
    + qty_: Qty
    + priority_: Priority
    + prev_order_: MEOrder*
    + next_order_: MEOrder*
  }
  
  ' Trading Side
  class MarketOrderBook {
    - bids_by_price_: MarketOrdersAtPrice*
    - asks_by_price_: MarketOrdersAtPrice*
    - price_orders_at_price_: array<MarketOrdersAtPrice*>
    - oid_to_order_: array<MarketOrder*>
    - order_pool_: MemPool<MarketOrder>
    - orders_at_price_pool_: MemPool<MarketOrdersAtPrice>
    - bbo_: BBO
  }
  
  class MarketOrdersAtPrice {
    + side_: Side
    + price_: Price
    + first_mkt_order_: MarketOrder*
    + prev_entry_: MarketOrdersAtPrice*
    + next_entry_: MarketOrdersAtPrice*
  }
  
  class MarketOrder {
    + order_id_: OrderId
    + side_: Side
    + price_: Price
    + qty_: Qty
    + priority_: Priority
    + prev_order_: MarketOrder*
    + next_order_: MarketOrder*
  }
  
  class BBO {
    + bid_price_: Price
    + ask_price_: Price
    + bid_qty_: Qty
    + ask_qty_: Qty
  }
  
  ' Relationships
  MEOrderBook "1" o-- "many" MEOrdersAtPrice : price levels
  MEOrdersAtPrice "1" o-- "many" MEOrder : FIFO queue
  MEOrdersAtPrice --> MEOrdersAtPrice : circular\ndoubly linked
  MEOrder --> MEOrder : circular\ndoubly linked
  
  MarketOrderBook "1" o-- "many" MarketOrdersAtPrice : price levels
  MarketOrdersAtPrice "1" o-- "many" MarketOrder : FIFO queue
  MarketOrdersAtPrice --> MarketOrdersAtPrice : circular\ndoubly linked
  MarketOrder --> MarketOrder : circular\ndoubly linked
  MarketOrderBook *-- BBO
}

note top of MEOrderBook
  **Exchange Order Book (MEOrderBook)**
  - Bids: Descending price (highest first)
  - Asks: Ascending price (lowest first)
  - Price levels: Doubly linked list
  - Orders at price: Doubly linked list (FIFO)
  - Time-Price Priority matching
end note

note top of MarketOrderBook
  **Trading Client Order Book (MarketOrderBook)**
  - Replica of exchange book
  - Rebuilt from market data updates
  - Used for trading decisions
  - Maintains BBO (best bid/offer)
end note

note bottom
  **Data Structure Visual:**
  
  BIDS (Descending):                    ASKS (Ascending):
  ┌─────────────────┐                   ┌─────────────────┐
  │ Price: $150.00  │ ← bids_by_price_  │ Price: $150.05  │ ← asks_by_price_
  │ Side: BUY       │                   │ Side: SELL      │
  │ ├─Order1(100)   │                   │ ├─Order5(50)    │
  │ ├─Order2(200) ←─┼─ circular FIFO   │ ├─Order6(150) ←─┼─ circular FIFO
  │ └─Order3(50)    │                   │ └─Order7(75)    │
  ├─────────────────┤                   ├─────────────────┤
  │ Price: $149.95  │                   │ Price: $150.10  │
  │ Side: BUY       │                   │ Side: SELL      │
  │ └─Order4(300)   │                   │ └─Order8(100)   │
  └─────────────────┘                   └─────────────────┘
         ↓                                       ↓
    (next_entry)                            (next_entry)
    Circular                                 Circular
    
  **Operations Complexity:**
  - Add order: O(1) at price level
  - Cancel order: O(1) with pointer
  - Match order: O(1) per matched order
  - Get BBO: O(1) (cached pointers)
  - Memory: Pre-allocated pools
end note

@enduml
```

---

## Summary

This low-latency trading system demonstrates:

1. **Architecture**: Separation of exchange and trading client with lock-free communication
2. **Performance**: Sub-microsecond latency for critical paths (order book operations)
3. **Data Structures**: Custom doubly-linked lists with memory pools (no heap allocation)
4. **Communication**: TCP for reliable order flow, UDP multicast for market data
5. **Risk Management**: Pre-trade checks (position limits, loss limits)
6. **Trading Strategies**: Market maker (provide liquidity) and liquidity taker (consume liquidity)
7. **Features**: Fair price calculation, momentum detection

**Key Performance Metrics:**
- Order book operation: 20-50 ns
- Lock-free queue: 10-20 ns
- End-to-end order latency: 5-20 μs (co-located)
- Market data latency: 1-10 μs

**Design Principles:**
- Lock-free (no mutexes)
- Memory pools (no heap)
- Fixed-point arithmetic (no floating-point for prices)
- Single-threaded matching engine (no contention)
- Pre-allocated buffers (deterministic latency)
