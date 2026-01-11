# Training Env
class MarginTradingEnv(gym.Env):
      
    def __init__(self, file_path, leverage=LEVERAGE, initial_balance=INITIAL_BALANCE, risk_free_rate=RISK_FREE_RATE, unit=UNIT, env_debug=False, sim_debug=False):
        
        super(MarginTradingEnv, self).__init__()
                
        self.debug = env_debug

        raw_df = pd.read_csv(file_path)
        
        raw_df = raw_df.fillna(method='ffill')
        raw_df = raw_df.dropna()


        self.OHLCV_df = process_raw_data(raw_df.copy(), '1min')
        
        self.OHLCV_df = self.OHLCV_df.fillna(method='ffill')
        self.OHLCV_df = self.OHLCV_df.dropna()
        
        # print(self.OHLCV_df)


        self.signal_df = self.calculate_all_signals(raw_df)
                
        self.signal_df = self.signal_df.fillna(method='ffill')
        self.signal_df = self.signal_df.dropna()
        
        self.simulator = MarginTradeSimulator(
            self.OHLCV_df['Close'], leverage, initial_balance, risk_free_rate, sim_debug)   
           
        self.unit = unit
        
        self.action_space = Discrete(201)  # 100 in positive, 100 in negative direction, and 0
        
        num_summary_fields = 6
        obs_space_shape = (len(self.calculate_price_df(self.OHLCV_df).columns) + len(self.signal_df.columns) +
                           len(self.simulator.account_info) + len(self.simulator.open_positions) + num_summary_fields,)
        
        self.observation_space = spaces.Box(
            low=-np.inf, high=np.inf, shape=obs_space_shape, dtype=np.float32)
        
        # Existing initialization...
        # self.trailing_stop_loss = None
        # self.trailing_stop_distance = trailing_stop_distance  # For example, 2% trailing stop

        self.hwm = initial_balance  # Start with the initial balance as the first HWM
        self.equity_drop_threshold = HWM_DROP  # 30% drop threshold

    def reset(self, *, seed=None, options=None):
        
        self.simulator.reset()

        # Collect indices of timestamps with time 00:01:00
        start_indices = []
        for idx, timestamp in enumerate(self.OHLCV_df.index):
            if timestamp.time() == datetime.time(0, 0):
                start_indices.append(idx)

        # Randomly choose an index from the start_indices
        if start_indices:
            random.seed(seed)
            chosen_index = random.choice(start_indices)
            self.simulator.timestep = chosen_index
        else:
            # If no indices with time 00:01:00 are found, start from the first non-NaN value as before
            first_valid_index = max(self.signal_df.reset_index().index[0], self.calculate_price_df(self.OHLCV_df).reset_index().index[0])
            self.simulator.timestep = first_valid_index

        # Extract timestamp (assuming the timestamp is in a column named 'Time')
        current_timestamp = self.OHLCV_df.index[self.simulator.timestep]

        self.hwm = self.simulator.account_info['equity']
        self.stop_trading = False

        # Pass the timestamp to the simulator
        self.simulator.update_timestamp(current_timestamp)

        return self._get_observation(), {}
        
    def step(self, action):
        # Convert the discrete action to a percentage in the range [-1, 1]
        percentage = self.action_to_value(action)

        # Extract timestamp
        current_timestamp = self.OHLCV_df.index[self.simulator.timestep]
        current_hour_gmt = current_timestamp.hour

        # Check if trading is allowed
        trading_allowed = (START_TRADING_TIME <= current_hour_gmt < END_TRADING_TIME) & (self.simulator.timecount % BATCH_INTERVAL == 0)

        # Update simulator state
        self.simulator.update_timestamp(current_timestamp)
        self.simulator.timestep += 1
        self.simulator.timecount += 1
        
        # Get current price
        price = self.simulator.price_series[self.simulator.timestep]

        # Get Kijun signals
        # signal_Tenkan_Kijun_4hour = self.signal_df.loc[current_timestamp, 'signal_Tenkan_Kijun_4hour']
        # signal_Close_SpanAB_4hour = self.signal_df.loc[current_timestamp, 'signal_Close_SpanAB_4hour']
        signal_MACD_histogram_4hour = self.signal_df.loc[current_timestamp, 'signal_MACD_histogram_4hour']
        # signal_GMMA_3_60_4hour = self.signal_df.loc[current_timestamp, 'signal_GMMA_3_60_4hour']
        signal_bbstop_4hour = self.signal_df.loc[current_timestamp, 'signal_bbstop_4hour']
        # signal_AO_4hour = self.signal_df.loc[current_timestamp, 'signal_AO_4hour']

        # Determine action type based on Kijun signals, percentage, and current NOP
        action_type = 0  # Default to hold

        # Check for signal consistency between 5-minute and 15-minute Kijun signals
        consistent_signal = (signal_MACD_histogram_4hour == signal_bbstop_4hour)

        # Close positions if opposite signal on 15-minute Kijun
        if consistent_signal == 1 and self.simulator.nop < 0:
            self.simulator.close(price)
        elif consistent_signal == -1 and self.simulator.nop > 0:
            self.simulator.close(price)

        # Execute trades only if signals are consistent
        if consistent_signal:
            if consistent_signal == 1 and percentage > 0:
                action_type = 1  # Long
            elif consistent_signal == -1 and percentage < 0:
                action_type = 2  # Short
                percentage = abs(percentage)  # Ensure percentage is positive


        # Handle end-of-day
        is_end_of_day = self.simulator.is_end_of_day()

        self.simulator.check_goal(price)
        self.simulator.check_stopped_out(price)

        # Check if current equity has dropped more than the threshold from HWM
        if self.simulator.account_info['equity'] < self.hwm * (1 - self.equity_drop_threshold):
            # Close all positions if the equity drop is more than the threshold
            self.simulator.close(price)
            # Optional: set a flag to stop trading if needed
            self.stop_trading = True
        
        if not is_end_of_day and not self.simulator.stopped_out and not self.simulator.goal_reached and not self.stop_trading: 

            # Calculate volume to trade
            volume = 0
            if trading_allowed and not is_end_of_day and price != 0 and self.simulator.leverage != 0:
                margin_to_use = self.simulator.equity * percentage
                volume = int(margin_to_use / (price / self.simulator.leverage))
                volume = (volume // self.unit) * self.unit

            # Adjust volume for sell actions
            adjusted_volume = -volume if action_type == 2 else volume

            # Calculate potential NOP
            potential_nop = self.simulator.nop + adjusted_volume * price

            # Handle NOP constraints
            if abs(potential_nop) > MAX_NOP:
                # Calculate the maximum allowable volume
                max_allowable_volume = (MAX_NOP - abs(self.simulator.nop)) / price
                volume = (int(max_allowable_volume) // self.unit) * self.unit
                adjusted_volume = -volume if action_type == 2 else volume
            else:
                self.simulator.invalid_action = False

            # Execute action
            self.simulator.execute(action_type, price, volume)

        else:
            self.simulator.close(price)
      

        # Update the HWM if current equity is higher
        self.hwm = max(self.hwm, self.simulator.account_info['equity'])

        # Determine if the episode is done
        terminated = False
        truncated = False
        
        if is_end_of_day:
            truncated = True
        
        if self.simulator.stopped_out or self.simulator.goal_reached:
            terminated = True

        reward = self.simulator.calculate_reward()

        # If the goal has been reached, add a bonus to the reward
        if self.simulator.goal_reached:
            reward += 2e4

        # Get observation
        observation = self._get_observation()

        # Handle NaN in observation
        if np.isnan(observation).any():
            print(f"Warning: NaN in observation at timestep {self.simulator.timestep}")

        # Handle NaN reward
        if np.isnan(self.simulator.reward):
            print(f"Warning: NaN reward at timestep {self.simulator.timestep}")
            self.simulator.reward = 0

        # Construct info dictionary
        info = {
            'account_info': self.simulator.account_info,
            'open_positions': self.simulator.open_positions,
            'closed_trades': self.simulator.done_trades,
            'record': self.simulator.record,
        }

        return observation, reward, terminated, truncated, info

    def action_to_value(self, action):
        # Convert discrete action index to corresponding value in range [-1, 1]
        # Note that we are subtracting 100, not 200
        return (action - 100) * 0.01

    def render(self, mode='human', action=None, reward=None):

        current_price_data = self.calculate_price_df(
            self.OHLCV_df).iloc[self.simulator.timestep]
        current_signal_data = self.signal_df.iloc[self.simulator.timestep]

        print("#" * 100)
        print(f"TimeStep: {self.simulator.timestep}")
        print(f"Action: {action}")
        print("Observation:")
        print("-" * 40)
        print("Price Data:")
        print(current_price_data.to_string())
        print("-" * 40)

        # if self.sim_debug_signals:
        #     print("Signal Data:")
        #     print(current_signal_data.to_string())

        print("-" * 40)
        print("Account Info:")
        for key, value in self.simulator.account_info.items():
            print(f"{key}: {value}")
        print("-" * 40)
        print("Open Positions:")
        for key, value in self.simulator.open_positions.items():
            print(f"{key}: {value}")
        print("-" * 40)
        print("Done Trades:")
        for trade in self.simulator.done_trades:
            trade_str = ", ".join(
                [f"{key.capitalize()}: {value:,.5f}" if key in ['price'] else f"{key.capitalize()}: {value:,.2f}" if isinstance(
                    value, (int, float)) else f"{key.capitalize()}: {value}" for key, value in trade.items()])
            print(f"{{{trade_str}}}")
        
        print("-" * 40)
        if reward is not None:
            print(f"Reward: {reward}")
        
        
        # if reward_components:
            # print("-" * 40)
            # print("Reward Breakdown:")
            # for key, value in reward_components.items():
            #     print(f"{key.capitalize()}: {value}")

        print("-" * 40)
        print(f"Reward: {reward}")
   
    # def _set_trailing_stop(self, side, price):
    #     # Set or update the trailing stop when a new position is opened
    #     if side == "buy":
    #         self.trailing_stop_loss = price * (1 - self.trailing_stop_distance)
    #     elif side == "sell":
    #         self.trailing_stop_loss = price * (1 + self.trailing_stop_distance)

    # def _update_and_check_trailing_stop(self, current_price):
    #     if self.simulator.nop > 0:  # Long position
    #         if self.trailing_stop_loss is None or current_price <= self.trailing_stop_loss:
    #             self.trailing_stop_loss = max(self.trailing_stop_loss or 0, current_price * (1 - self.trailing_stop_distance))
    #         if current_price <= self.trailing_stop_loss:
    #             self.simulator.close(current_price)  # Close the position
    #     elif self.simulator.nop < 0:  # Short position
    #         if self.trailing_stop_loss is None or current_price >= self.trailing_stop_loss:
    #             self.trailing_stop_loss = min(self.trailing_stop_loss or float('inf'), current_price * (1 + self.trailing_stop_distance))
    #         if current_price >= self.trailing_stop_loss:
    #             self.simulator.close(current_price)  # Close the position

    def _get_observation(self):
        # Get current price data
        current_price_data = self.calculate_price_df(self.OHLCV_df).iloc[self.simulator.timestep].values

        # Get current signal data
        current_signal_data = self.signal_df.iloc[self.simulator.timestep].values

        # Get account_info data
        account_info_data = np.array(list(self.simulator.account_info.values()))

        # Get open_positions data
        open_positions_data = np.array(list(self.simulator.open_positions.values()))

        # Get summary of done trades
        done_trades_summary = np.array(list(self.simulator.summarize_done_trades().values()))

        # Concatenate all arrays into a single observation array
        observation = np.concatenate((current_price_data, current_signal_data,
                                    open_positions_data, account_info_data, done_trades_summary), axis=0)

        # Convert all values to a numeric type, coercing non-numeric values to NaN
        observation = observation.astype(float)

        # Replace NaN values and infinite values with zero and large finite numbers, respectively
        observation = np.nan_to_num(observation)

        return observation

    def set_new_episode(self, episode):
        self.simulator.price_series = episode
        self.simulator.timestep = 0

    def calculate_all_signals(self, OHLCV_df):
        
        df_1min_signals = calculate_signals(OHLCV_df, "1min")
        df_5min_signals = calculate_signals(OHLCV_df, "5min")
        df_15min_signals = calculate_signals(OHLCV_df, "15min")
        df_1h_signals = calculate_signals(OHLCV_df, "1hour")
        df_4h_signals = calculate_signals(OHLCV_df, "4hour")
        df_1day_signals = calculate_signals(OHLCV_df, "1day")
        

        concatenated_df = pd.concat(
            [df_1min_signals, df_5min_signals, df_15min_signals, df_1h_signals, df_4h_signals, df_1day_signals], axis=1)
        concatenated_df = concatenated_df.fillna(method='ffill')
        df = concatenated_df.dropna()
        
        # print(df.columns)

        return df

    def calculate_price_df(self, input_df):

        price_df = input_df.copy()
        # price_df['Time'] = pd.to_datetime(price_df['Time'], format='%Y.%m.%d %H:%M:%S')
        # price_df['Time'] = price_df['Time'].apply(lambda x: x.replace(second=0))
        # price_df.set_index('Time', inplace=True)

        price_df['ATR'] = atr(
            price_df['High'], price_df['Low'], price_df['Close'])

        # Normalize the price using ATR
        price_df['Normalized_Open'] = price_df['Open'] / price_df['ATR'] * 1e-6
        price_df['Normalized_High'] = price_df['High'] / price_df['ATR'] * 1e-6
        price_df['Normalized_Low'] = price_df['Low'] / price_df['ATR'] * 1e-6
        price_df['Normalized_Close'] = price_df['Close'] / \
            price_df['ATR'] * 1e-6

        price_df['High_Low'] = price_df['High'] - price_df['Low']

        price_df['HL_ATR_r'] = (price_df['High_Low'] / price_df['ATR']) * 1e-6

        # price_df.drop('Volume', axis=1, inplace=True)

        price_df = price_df.fillna(method='ffill')
        price_df = price_df.dropna()

        return price_df

    @property
    def account_info(self):
        return self.simulator.account_info

    @property
    def open_positions(self):
        return self.simulator.open_positions

    @staticmethod
    def create_episodes(price_series, episode_length):
        daily_groups = price_series.groupby(price_series.index.date)

        episodes = []
        for day, group in daily_groups:
            if len(group) >= episode_length:
                for i in range(len(group) - episode_length + 1):
                    episodes.append(group.iloc[i:i+episode_length].values)
 
        return episodes            
           