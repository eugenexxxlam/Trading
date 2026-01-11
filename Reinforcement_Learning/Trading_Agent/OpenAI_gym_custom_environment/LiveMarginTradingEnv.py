
# Live Env        
class LiveMarginTradingEnv(gym.Env):
    
    def __init__(self, public_key, secret_key, symbol):
        super(LiveMarginTradingEnv, self).__init__()
        
        # Initialize Bybit exchange and assign it to self.exchange
        self.exchange = ccxt.bybit({
            'apiKey': public_key,
            'secret': secret_key,
            'enableRateLimit': True
        })
        self.exchange.set_sandbox_mode(True)
        
        # Initialize the session
        self.session = HTTP(
            testnet=True,  # Set to False if using the live environment
            api_key=public_key,
            api_secret=secret_key
        )
        
        self.symbol = symbol
        self.leverage = LEVERAGE
        self.initial_balance = 0
        self.risk_free_rate = RISK_FREE_RATE
        self.unit = UNIT
        self.timestep = 0  # Initialize timestep
        
        # Initialize historical data
        self.equity_history = [self.initial_balance]  # Start with initial balance
        self.return_history = [0]  # Start with no return
        self.drawdowns = [0]  # Start with no drawdown
        
        # Initialize performance metrics
        self.max_drawdown = 0
        self.sortino_ratio = 0
        
        # Initialize trading related variables
        self.nop = 0  # Net open position
        self.avg_price = 0  # Average price
        self.spot_price = 0  # Current spot price

        # Dummy observation space for now
        self.action_space = Discrete(201)  # 100 in positive, 100 in negative direction, and 0
        
        num_summary_fields = 6
        
        obs_space_shape = (len(self._calculate_price_df().columns) + len(self._calculate_and_concatenate_signals().columns) +
                           len(self._fetch_account_info()) + len(self._fetch_open_positions()) + num_summary_fields,)
        
        self.observation_space = spaces.Box(
            low=-np.inf, high=np.inf, shape=obs_space_shape, dtype=np.float32)

        self.previous_account_info = {}
        # self.account_info = {}
        
        self.account_info = self._fetch_account_info()
        print("account initialization:", self.account_info)
        
        
        
        self.open_positions = {}
        self.done_trades = []
        self.record = []
        
        # Price series
        self.timestep = 0
        self.timecount = 0
        self.episode_start_index = 0
        self.timestamp = None # Initialize timestamp
        
        self.spot_price = 0

        # Account balance and equity
        
        self.balance = 0
        self.equity_history = []
        self.reward = 0

        # Profit and Loss
        self.realized_pnl = 0
        self.unrealized_pnl = 0

        # Positions
        self.nop = 0
        self.avg_price = 0
        
        # Leverage and margin
        self.margin_required = 0
        self.margin_free = self.balance
        self.margin_level = 100
        self.portfolio_leverage = 0

        # Performance metrics
        self.return_percentage = 0
        self.log_return = 0
        self.max_drawdown = 0
        self.sortino_ratio = 0
        self.accumulated_cost = 0
               
        # Trading details
        self.trade_not_executed = False

        self.stopped_out = False
        self.goal_reached = False
                    
    def _action_to_value(self, action):
        value = (action - 100) * 0.01
        print(f"Converted action {action} to value: {value}")
        return value

    def _calculate_and_concatenate_signals(self):
        timeframes = ['1m', '5m', '15m', '1h', '1d']
        signals_dfs = []

        for tf in timeframes:
            ohlcv_df = self._fetch_ohlcv_data(tf)
            # Adjust the timeframe format to match the process_raw_data() function
            adjusted_tf = tf.replace('m', 'min').replace('h', 'hour').replace('d', 'day')
            processed_df = process_raw_data(ohlcv_df, adjusted_tf)
            signals_df = calculate_signals(processed_df, adjusted_tf)
            signals_dfs.append(signals_df)

        concatenated_signals_df = pd.concat(signals_dfs, axis=1)
        concatenated_signals_df.fillna(method='ffill', inplace=True)
        concatenated_signals_df.dropna(inplace=True)

        return concatenated_signals_df
      
    def _calculate_nop(self, positions):
        nop = 0
        for position in positions:
            if position['symbol'] == 'BTC/USDT:USDT':
                contracts = float(position['contracts'])
                entry_price = float(position['entryPrice'])
                side = position['info']['side']
                
                if side == 'Buy':
                    nop += contracts * entry_price
                elif side == 'Sell':
                    nop -= contracts * entry_price
        return nop

    def _calculate_price_df(self):
        
        input_df = self._fetch_ohlcv_data('1d')
        price_df = input_df.copy()
        
        price_df['Time'] = pd.to_datetime(price_df['Time'], format='%Y.%m.%d %H:%M:%S')
        price_df['Time'] = price_df['Time'].apply(lambda x: x.replace(second=0))
        price_df.set_index('Time', inplace=True)
        
        price_df.drop('Volume', axis=1, inplace=True)
        
        price_df['ATR'] = atr(price_df['High'], price_df['Low'], price_df['Close'])

        # Normalize the price using ATR
        price_df['Normalized_Open'] = price_df['Open'] / price_df['ATR'] * 1e-6
        price_df['Normalized_High'] = price_df['High'] / price_df['ATR'] * 1e-6
        price_df['Normalized_Low'] = price_df['Low'] / price_df['ATR'] * 1e-6
        price_df['Normalized_Close'] = price_df['Close'] / price_df['ATR'] * 1e-6

        price_df['High_Low'] = price_df['High'] - price_df['Low']
        price_df['HL_ATR_r'] = (price_df['High_Low'] / price_df['ATR']) * 1e-6
        
        price_df = price_df.fillna(method='ffill').dropna()

        return price_df
       
    def _calculate_reward(self, weight_dense=0.8, weight_sparse=1, weight_shaping=0.3,
                        holding_reward=0.5, trading_cost_penalty=0.1, 
                        stop_out_penalty=20, realized_pnl_weight=2, 
                        invalid_action_penalty=0.0005, 
                        debug=False):
        
        current_equity = self.account_info["equity"]
        previous_equity = self.previous_account_info["equity"]
        initial_balance = self.initial_balance
        sortino_ratio = self.account_info["sortino_ratio"]
        stopped_out = self.stopped_out
        unrealized_pnl = self.account_info["unrealized_pnl"]
        nop = self.account_info["nop"]
        accumulated_cost = self.account_info["accumulated_cost"]
        realized_pnl = self.realized_pnl

        # Dense reward based on unrealized PnL
        dense_reward = unrealized_pnl * weight_dense

        # Holding reward for profitable positions
        holding_reward_value = holding_reward if nop != 0 and unrealized_pnl > 0 else 0

        # Trading cost penalty
        trading_cost_penalty_value = -min(accumulated_cost, trading_cost_penalty)

        # Shaping reward based on Sortino ratio
        shaping_reward = max(0, sortino_ratio) * weight_shaping

        # Penalty for invalid actions
        invalid_action_reward = -invalid_action_penalty if self.trade_not_executed else 0

        # Sparse reward based on log return
        log_return = np.log(current_equity / initial_balance) if current_equity > 0 and initial_balance > 0 else 0
        sparse_reward = log_return * weight_sparse

        # Realized PnL reward
        realized_pnl_reward = realized_pnl * realized_pnl_weight

        # Calculate total reward
        total_reward = dense_reward + holding_reward_value + trading_cost_penalty_value + shaping_reward + invalid_action_reward + realized_pnl_reward + sparse_reward

        if stopped_out:
            stop_out_reward = -stop_out_penalty
            total_reward += stop_out_reward

        # Debug information
        if debug:
            print(f"Current Equity: {current_equity:.2f}, Previous Equity: {previous_equity:.2f}, Initial Balance: {initial_balance:.2f}")
            print(f"Sortino Ratio: {sortino_ratio:.2f}, Dense Reward: {dense_reward:.6f}, Scaled Dense Reward: {dense_reward:.6f}")
            print(f"Holding Reward: {holding_reward_value:.6f}, Trading Cost Penalty: {trading_cost_penalty_value:.6f}")
            print(f"Scaled Shaping Reward: {shaping_reward:.6f}, Invalid Action Reward: {invalid_action_reward:.6f}")
            print(f"Realized PnL Reward: {realized_pnl_reward:.6f}, Sparse Reward (Log Return): {sparse_reward:.6f}")
            print(f"Total Reward: {total_reward:.6f}")

        return total_reward

    def _calculate_daily_pnl_and_cost(self, today_date):
        done_trades = []
        cursor = None
        fee_rate = 0.000395 * 2  # Adjusted fee rate

        # Convert today's date to timestamp
        start_timestamp = int(datetime.datetime.strptime(today_date, "%Y-%m-%d").timestamp() * 1000)
        end_timestamp = start_timestamp + 86400000  # Add one day in milliseconds

        while True:
            params = {
                "category": "linear",
                "limit": 100,
                "startTime": start_timestamp,
                "endTime": end_timestamp,
                "cursor": cursor
            }
            response = self.session.get_closed_pnl(**params)

            if response['retCode'] != 0:
                break

            pnl_list = response['result']['list']
            cursor = response['result'].get('nextPageCursor', None)

            for pnl in pnl_list:
                trade_fee = float(pnl['orderPrice']) * float(pnl['qty']) * fee_rate
                done_trades.append({
                    'closedPnl': pnl['closedPnl'],
                    'tradingFee': trade_fee
                })

            if not cursor:
                break

        # Calculate daily PnL and total trading fee
        daily_pnl = sum(float(trade['closedPnl']) for trade in done_trades)
        total_trading_fee = sum(trade['tradingFee'] for trade in done_trades)

        return daily_pnl, total_trading_fee

    def _check_session_end(self, timestamp):
        # Implement logic to check if the trading session should end
        return False, False  # Placeholder for now

    def _check_trading_allowed(self, timestamp):
        # Check if current hour is within trading hours
        current_hour = timestamp.hour
        
        
        return START_TRADING_TIME <= current_hour < END_TRADING_TIME

    def _close_all_position(self):
        
        positions = self.exchange.fetch_positions()

        # Close all positions in smaller chunks
        for position in positions:
            symbol = position['symbol']
            type = 'market'
            full_amount = abs(float(position['contracts']))
            chunks = 1  # Number of smaller chunks to split the position into
            chunk_size = full_amount / chunks  # Size of each chunk

            if position['side'].lower() == 'long':
                side = 'sell'
            elif position['side'].lower() == 'short':
                side = 'buy'
            else:
                continue  # Skip if position side is not recognized

            for i in range(chunks):
                print(f"Closing chunk {i+1}/{chunks} of {side} position for {symbol}, size: {chunk_size}")
                order = self.exchange.create_order(symbol, type, side, chunk_size)
                print("Order response:", order)
                time.sleep(self.exchange.rateLimit / 1000)  # Respect rate limits

    def _convert_usdt_to_btc(self, usdt_amount, btc_price):
        print(f"Converting USDT amount {usdt_amount} to BTC at price {btc_price}")
        btc_amount = usdt_amount / btc_price
        print(f"Converted to BTC amount: {btc_amount}")
        return btc_amount

    def _determine_action_type(self, percentage):
        if percentage < 0:
            action_type = 'sell'  # Sell
            percentage = abs(percentage)
        elif percentage > 0:
            action_type = 'buy'  # Buy
        else:
            action_type = 'hold'  # Hold
        print(f"Determined action type: {action_type} for percentage: {percentage}")
        return action_type, percentage
           
    def _execute_trade(self, action):
        
        self.previous_account_info = self.account_info.copy()
        
        symbol = 'BTCUSDT'  # Symbol for trading
        
        percentage = self._action_to_value(action)
        action_type, adjusted_percentage = self._determine_action_type(percentage)

        known_risk_limit = MAX_NOP
        leverage = LEVERAGE

        # Fetch and calculate initial NOP
        positions = self.exchange.fetch_positions()
        initial_nop = self._calculate_nop(positions)
        print(f"Initial NOP: {initial_nop}")
        
          # Fetch current Kijun signals
        current_signals = self._calculate_and_concatenate_signals().iloc[-1]
        kijun_signal_15 = current_signals['signal_Kijun_15min']
        kijun_signal_5 = current_signals['signal_Kijun_5min']
        consistent_signal = (kijun_signal_15 == kijun_signal_5)
        
            # Execute trades only if signals are consistent
        if not consistent_signal:
            print("Kijun signals are not consistent. Trade not executed.")
            self.trade_not_executed = True
            return

        # Fetch balance information and calculate order amounts
        balance_info = self.exchange.fetch_balance()
        # print("Balance Info:", balance_info)  # Debug: Print the entire balance info

        usdt_balance = next((coin for coin in balance_info['info']['result']['list'][0]['coin'] if coin['coin'] == 'USDT'), {})
        margin_free = float(usdt_balance.get('availableToWithdraw', 0))
        equity = float(usdt_balance.get('equity', 0))

        print(f"Margin Free: {margin_free}, Equity: {equity}, Adjusted Percentage: {adjusted_percentage}")

        # Calculate the total USDT amount for the order
        total_usdt_amount = adjusted_percentage * equity * leverage

        # Calculate the BTC amount to be ordered based on current price
        orderbook = self.exchange.fetch_order_book(symbol)
        fifth_best_bid = orderbook['asks'][4][0]
        fifth_best_ask = orderbook['bids'][4][0]
        price = fifth_best_bid if action_type == 'buy' else fifth_best_ask
        btc_amount_to_order = self._convert_usdt_to_btc(total_usdt_amount, price)

        # Calculate the desired NOP after the trade
        desired_nop = initial_nop + (total_usdt_amount if action_type == 'buy' else -total_usdt_amount)

        # Check if the desired NOP exceeds the known risk limit
        if abs(desired_nop) > known_risk_limit:
            print("Desired NOP exceeds the risk limit. Adjusting the order size.")
            adjusted_usdt_amount = known_risk_limit - abs(initial_nop)
            adjusted_usdt_amount = min(adjusted_usdt_amount, total_usdt_amount)
            btc_amount_to_order = self._convert_usdt_to_btc(adjusted_usdt_amount, price)

        # Place orders within the risk limit
        order_size = 1.0  # Adjust based on your strategy
        minimum_order_size = 0.001  # Minimum BTC amount per order as per exchange's requirement

        while btc_amount_to_order > 0:
            size_of_this_order = min(order_size, btc_amount_to_order)
            if size_of_this_order < minimum_order_size:
                print(f"Order size {size_of_this_order} is below the minimum threshold.")
                break
            try:
                order = self.exchange.create_order(symbol, 'market', action_type, size_of_this_order, price)
                # print(f"Order placed: {order}")
            except Exception as e:
                print(f"An error occurred while placing the order: {e}")
            btc_amount_to_order -= size_of_this_order
            time.sleep(self.exchange.rateLimit / 1000)

        # Recalculate NOP after trading
        positions = self.exchange.fetch_positions()
        final_nop = self._calculate_nop(positions)
        print(f"Final NOP: {final_nop}")

    def _fetch_account_info(self):
        # Fetch account balance
        balance_info = self.exchange.fetch_balance()

        # Extract USDT balance details
        usdt_balance = next((coin for coin in balance_info['info']['result']['list'][0]['coin'] if coin['coin'] == 'USDT'), {})

        
        # print(balance_info)
        
        # Assigning values based on the provided details
        self.equity = float(usdt_balance.get('equity', 0))
        
        
        
        # Fetch and calculate daily PnL and accumulated costs
        today_date = datetime.datetime.utcnow().strftime('%Y-%m-%d')
        realized_pnl, accumulated_cost = self._calculate_daily_pnl_and_cost(today_date)
        
        
        # realized_pnl = float(usdt_balance.get('cumRealisedPnl', 0))
        
        
        unrealized_pnl = float(usdt_balance.get('unrealisedPnl', 0))
        self.balance = float(usdt_balance.get('availableToWithdraw', 0))
       
        margin_required = float(usdt_balance.get('totalPositionIM', 0))
        margin_free = float(usdt_balance.get('availableToWithdraw', 0))
        
         # Adjust initial_balance only at the start or if not set
        if self.timestep == 0 or self.initial_balance is None:
            self.initial_balance = max(0, self.equity - unrealized_pnl - realized_pnl)

        

        # Fetch open positions
        positions = self.exchange.fetch_positions()
        # print("Positions:", positions)

        # Initialize NOP
        nop = 0

        # Calculate NOP considering the side of each position
        for position in positions:
            if position['symbol'] == 'BTC/USDT:USDT':
                contracts = float(position['contracts'])
                entry_price = float(position['entryPrice'])
                side = position['info']['side']
                
                # Adjust the NOP based on the position side
                if side == 'Buy':
                    nop += contracts * entry_price
                elif side == 'Sell':
                    nop -= contracts * entry_price

        # print(f"Current NOP: {nop}")
        
        # print(f"initial_balance: {initial_balance}")
        
         # Shift values for log return calculation
        shift_constant = max(abs(self.initial_balance), 1.0)  # Ensure a minimum shift of 1.0
        shifted_equity = self.equity + shift_constant
        shifted_initial_balance = self.initial_balance + shift_constant

        # Performance metrics
        self.return_percentage = ((self.equity - self.initial_balance) / self.initial_balance) * 100 if self.initial_balance != 0 else 0

        # Calculate log return using shifted values
        self.log_return = np.log(shifted_equity / shifted_initial_balance) if shifted_initial_balance > 0 else 0
        # print(f"return_percentage: {return_percentage}")
        # print(f"log_return: {log_return}")

        # Margin and leverage calculations
        margin_level = self.equity / margin_required * 100 if margin_required else float('inf')
        portfolio_leverage = abs(nop) / self.equity if self.equity else 0
               
        # Calculate Sortino ratio (considering risk-free rate)
        if len(self.equity_history) > 1:
            returns = np.array(self.equity_history)
            daily_returns = np.diff(returns) / returns[:-1]
            daily_excess_returns = daily_returns - self.risk_free_rate / 252

            # Calculate downside deviation (volatility of negative excess returns)
            negative_excess_returns = daily_excess_returns[daily_excess_returns < 0]
            
            if len(negative_excess_returns) > 1:
                downside_deviation = np.std(negative_excess_returns)
            else:
                downside_deviation = 0

            if downside_deviation != 0:
                self.sortino_ratio = np.mean(daily_excess_returns) / downside_deviation
            else:
                self.sortino_ratio = 0
        else:
            self.sortino_ratio = 0
            
            
        # Calculate drawdown
        
        self.running_max_equity = np.maximum.accumulate(self.equity_history)
        self.drawdowns = (self.running_max_equity - self.equity_history) / self.running_max_equity
        self.max_drawdown = np.max(self.drawdowns) * 100 if len(self.drawdowns) > 0 else 0
        
        # print("self.running_max_equity: ", self.running_max_equity)
        # print("self.drawdowns: ", self.drawdowns)
        # print("self.max_drawdown: ", self.max_drawdown)

        # Construct account info dictionary
        account_info = {
            "timestep": self.timestep,
            "equity": self.equity,  # Directly as float
            "balance": self.balance,  # Directly as float
            "nop": nop,  # Directly as integer
            "realized_pnl": realized_pnl,  # Directly as float
            "unrealized_pnl": unrealized_pnl,  # Directly as float
            "accumulated_cost": accumulated_cost,  # Directly as float
            "margin_required": margin_required,  # Directly as float
            "margin_free": margin_free,  # Directly as float
            "margin_level_%": margin_level,  # Directly as float
            "portfolio_leverage": portfolio_leverage,  # Directly as float
            "return_percentage": self.return_percentage,  # Directly as float
            "log_return": self.log_return,  # Directly as float
            "max_drawdown": self.max_drawdown,  # Directly as float
            "sortino_ratio": self.sortino_ratio,  # Directly as float
            "reward": 0  # Placeholder or based on your reward strategy
        }
        
        return account_info
        
    def _fetch_ohlcv_data(self, timeframe):
        # self.rate_limiter.wait()
        time.sleep(0.1)
        limit = 120
        ohlcv = self.exchange.fetch_ohlcv(self.symbol, timeframe, limit=limit)
        columns = ['Time', 'Open', 'High', 'Low', 'Close', 'Volume']
        ohlcv_df = pd.DataFrame(ohlcv, columns=columns)
        ohlcv_df['Time'] = pd.to_datetime(ohlcv_df['Time'], unit='ms')
        return ohlcv_df

    def _fetch_open_positions(self):
        # Fetch open positions
        positions = self.exchange.fetch_positions()

        # Initialize variables for open position dictionary
        total_contracts_btc = 0
        total_entry_value_usd = 0
        total_unrealized_pnl_usd = 0
        trade_not_executed = 0  # Assuming no trades are pending initially
        stopped_out = 0  # Assuming no positions are stopped out initially

        # Initialize NOP
        nop_usd = 0

        # Process each position
        for position in positions:
            contracts = float(position['contracts'])
            entry_price = float(position['entryPrice'])
            
            # Check for None value and assign a default if necessary
            unrealized_pnl = float(position['unrealizedPnl']) if position['unrealizedPnl'] is not None else 0.0

            side = position['info']['side']

            # Calculate NOP considering the side of each position
            if side == 'Buy':
                nop_usd += contracts * entry_price
            elif side == 'Sell':
                nop_usd -= contracts * entry_price

            # Update other totals
            total_contracts_btc += contracts
            total_entry_value_usd += contracts * entry_price
            total_unrealized_pnl_usd += unrealized_pnl

        # Current spot price (last trade price) - Replace 'BTC/USDT' with your symbol
        spot_price_usd = self.exchange.fetch_ticker('BTCUSDT')['bid']

        # Calculate average price if there are open positions
        avg_price_usd = total_entry_value_usd / total_contracts_btc if total_contracts_btc > 0 else 0

        # Construct the open position dictionary
        open_positions = {
            "nop": nop_usd,  # Directly as float
            "avg_price": avg_price_usd,  # Directly as float
            "spot_price": spot_price_usd,  # Directly as float
            "Floating_Profit": total_unrealized_pnl_usd,  # Directly as float
            "trade_not_executed": trade_not_executed,  # Directly as int
            "stopped_out": stopped_out  # Directly as int
        }
        
        return open_positions

    def _fetch_trades_summary(self):
        done_trades = []
        cursor = None
        fee_rate = 0.000395 * 2  # Adjusted fee rate
        today_date = datetime.datetime.utcnow().strftime('%Y-%m-%d')

        # Convert today's date to timestamp
        start_timestamp = int(datetime.datetime.strptime(today_date, "%Y-%m-%d").timestamp() * 1000)
        end_timestamp = start_timestamp + 86400000  # Add one day in milliseconds

        while True:
            params = {
                "category": "linear",
                "limit": 100,
                "startTime": start_timestamp,
                "endTime": end_timestamp,
                "cursor": cursor
            }
            response = self.session.get_closed_pnl(**params)

            if response['retCode'] != 0:
                break

            pnl_list = response['result']['list']
            cursor = response['result'].get('nextPageCursor', None)

            for pnl in pnl_list:
                trade_fee = float(pnl['orderPrice']) * float(pnl['qty']) * fee_rate
                done_trades.append({
                    'closedPnl': pnl['closedPnl'],
                    'tradingFee': trade_fee
                })

            if not cursor:
                break
           
        # Summarize trades
        num_trades = len(done_trades)
        total_profit = sum(float(trade['closedPnl']) for trade in done_trades)
        average_profit = total_profit / num_trades if num_trades > 0 else 0

        winning_trades = [trade for trade in done_trades if float(trade['closedPnl']) > 0]
        losing_trades = [trade for trade in done_trades if float(trade['closedPnl']) <= 0]

        num_winning_trades = len(winning_trades)
        num_losing_trades = len(losing_trades)
        win_rate = num_winning_trades / num_trades if num_trades > 0 else 0

        return {
            'num_trades': num_trades,
            'total_profit': total_profit,
            'average_profit': average_profit,
            'win_rate': win_rate,
            'num_winning_trades': num_winning_trades,
            'num_losing_trades': num_losing_trades,
        }

    def _get_observation(self):
                
        self.previous_account_info = self._fetch_account_info()
        # Get current price data
        current_price_data = self._calculate_price_df().iloc[-1].values

        # Get current signal data
        current_signal_data = self._calculate_and_concatenate_signals().iloc[-1].values

        # Correctly call the methods and then extract values from their return dictionaries
        account_info_data = np.array(list(self._fetch_account_info().values()))
        open_positions_data = np.array(list(self._fetch_open_positions().values()))

        done_trades_summary = np.array(list(self._fetch_trades_summary().values()))

        # Concatenate all arrays into a single observation array
        observation = np.concatenate((current_price_data, current_signal_data,
                                    open_positions_data, account_info_data, done_trades_summary), axis=0)

        # Convert all values to a numeric type, coercing non-numeric values to NaN
        # observation = observation.astype(float)

        # Replace NaN values and infinite values with zero and large finite numbers, respectively
        observation = np.nan_to_num(observation)

        return observation

    def _timestamp_to_utc(self, ts):
        return datetime.datetime.utcfromtimestamp(int(ts) / 1000).strftime('%Y-%m-%d %H:%M:%S')

    def _is_end_of_day(self):
        return self.timecount % MAX_TIMESTEP == 0

    def reset(self, *, seed=None, options=None):
        
        self.account_info = self._fetch_account_info()
        self.open_positions = self._fetch_open_positions()
        self.done_trades = self._fetch_trades_summary()
        
        # self.initial_balance = self.account_info['balance']        
                
        self._close_all_position()

        return self._get_observation(), {}
        
    def step(self, action):
        current_timestamp = datetime.datetime.now()

        # Fetch current Kijun signals
        current_signals = self._calculate_and_concatenate_signals().iloc[-1]
        kijun_signal_15 = current_signals['signal_Kijun_15min']
        kijun_signal_5 = current_signals['signal_Kijun_5min']
        consistent_signal = (kijun_signal_15 == kijun_signal_5)

        # Debug: Print the current timestamp and Kijun signals
        print(f"Current Timestamp: {current_timestamp}")
        print(f"kijun_signal_15: {kijun_signal_15}")
        print(f"kijun_signal_5: {kijun_signal_5}")
        print(f"consistent_signal: {consistent_signal}")

        # Determine if trading is allowed at the current timestamp
        trading_allowed = self._check_trading_allowed(current_timestamp)
        print(f"Trading Allowed: {trading_allowed}")
      
        
                # Fetch and calculate initial NOP
        positions = self.exchange.fetch_positions()
        
        nop = self._calculate_nop(positions)
        
        if (kijun_signal_15 == 1 and nop < 0) or (kijun_signal_15 == -1 and nop > 0):
            self._close_all_position()

        # Execute trades only if signals are consistent and trading is allowed
        if consistent_signal and trading_allowed and self.timestep != 0:
            # Execute the trade based on the action
            self._execute_trade(action)

        # Increment timestep
        self.timestep += 1

        # Fetch updated account information and open positions
        self.account_info = self._fetch_account_info()
        self.open_positions = self._fetch_open_positions()

        # Get the current observation
        observation = self._get_observation()

        # Calculate the reward
        reward = self._calculate_reward()

        # Check if the trading session should end
        terminated, truncated = self._check_session_end(current_timestamp)
        
        print(f"account_info: {self.account_info}")
        print(f"open_positions: {self.open_positions}")
        print(f"Reward: {reward}")
        print(f"Terminated: {terminated}, Truncated: {truncated}")

        # Construct the info dictionary
        info = {
            'account_info': self.account_info,
            'open_positions': self.open_positions,
            'closed_trades': self.done_trades,
        }

        return observation, reward, terminated, truncated, info

    def render(self, mode='human'):
        # Optional rendering
        pass

    def close(self):
        # Clean up resources if necessary
        pass
