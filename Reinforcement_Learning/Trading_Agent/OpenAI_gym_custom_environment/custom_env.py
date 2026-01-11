import json
import datetime
import time
import random
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import asyncio
import ccxt
import ccxt.pro as ccxtpro
import gymnasium as gym
from gymnasium import spaces
from gymnasium.spaces import Discrete, Box
import pandas_ta as ta
from pandas import DataFrame
from pandas_ta.utils import get_drift, get_offset, verify_series
from pandas_ta.trend import aroon
from pandas_ta.momentum import ao, willr, macd
from pandas_ta.overlap import ichimoku
from pandas_ta.volatility import bbands, atr
from pandas_ta.utils import recent_maximum_index, recent_minimum_index
from pybit.unified_trading import HTTP
import json
from prettytable import PrettyTable
import datetime


from pandas import DataFrame
from pandas_ta import Imports
from pandas_ta.utils import get_offset, verify_series
from pandas_ta.utils import recent_maximum_index, recent_minimum_index
import datetime
import time
# Constants for your training environment

START_TRADING_TIME = 1
END_TRADING_TIME = 21
MAX_NOP = 15_000_000
MAX_PROFIT_TARGET = 120
MAX_TIMESTEP = 1120
INITIAL_BALANCE = 100000
LEVERAGE = 100
RISK_FREE_RATE = 0.0003
UNIT = 1
CUT_LOSS = -25
STOP_OUT = 50
TRADING_FEE = 0.0008
BATCH_INTERVAL = 15


HWM_DROP = 0.15

def process_singal_ichimoku(df, tenkan=9, kijun=26, senkou=52, chikou_lag=22):
    # Calculate Ichimoku components
    ichimokudf, spandf = ta.ichimoku(df['High'], df['Low'], df['Close'], tenkan=tenkan, kijun=kijun, senkou=senkou)
    df = df.join(ichimokudf)
    # Initialize Ichimoku Kijun signal column
    df['signal_Kijun'] = 0
    df['signal_Tenkan_Kijun'] = 0
    df['signal_Close_SpanAB'] = 0
    df['signal_chikou'] = 0
    df['signal_cloud'] = 0

    # Compare Close with Kijun Sen
    df.loc[df['Close'] > df['IKS_26'], 'signal_Kijun'] = 1  # Bullish
    df.loc[df['Close'] < df['IKS_26'], 'signal_Kijun'] = -1 # Bearish

    # Compare Tenkan Sen with Kijun Sen
    df.loc[df['ITS_9'] > df['IKS_26'], 'signal_Tenkan_Kijun'] = 1   # Bullish
    df.loc[df['ITS_9'] < df['IKS_26'], 'signal_Tenkan_Kijun'] = -1  # Bearish

    # Compare Close with Span A and Span B
    df.loc[df['Close'] > df['ISA_9'], 'signal_Close_SpanAB'] = 1   # Bullish
    df.loc[df['Close'] < df['ISB_26'], 'signal_Close_SpanAB'] = -1  # Bearish

    # Compare Chikou Span with the closing price from 22 days ago
    df.loc[df['ICS_26'].shift(chikou_lag) > df['Close'].shift(chikou_lag), 'signal_chikou'] = 1  # Bullish
    df.loc[df['ICS_26'].shift(chikou_lag) < df['Close'].shift(chikou_lag), 'signal_chikou'] = -1 # Bearish

    # Compare Close with Span A and Span B
    df.loc[df['ISA_9'] > df['ISB_26'], 'signal_cloud'] = 1   # Bullish
    df.loc[df['ISA_9'] < df['ISB_26'], 'signal_cloud'] = -1  # Bearish


    return df

def process_signal_macd(df):
    # Assuming df is already processed by `process_raw_data`

    # Calculate MACD components
    macd_result = macd(df["Close"])
    if macd_result is not None:
        df = df.assign(**macd_result)

    # Calculate MACD Histogram
    df['MACD_Histogram'] = df['MACD_12_26_9'] - df['MACDs_12_26_9']

    # Vectorized function for MACD Histogram Signal
    df['signal_MACD_histogram'] = 0
    histogram_positive = df['MACD_Histogram'] > 0
    histogram_growing = df['MACD_Histogram'] > df['MACD_Histogram'].shift(1)
    df.loc[histogram_positive & histogram_growing, 'signal_MACD_histogram'] = 1
    df.loc[histogram_positive & ~histogram_growing, 'signal_MACD_histogram'] = -1

    def crossover_signal(df, column, signal_name, compare_to):
        df[signal_name] = 0
        current = df[column]
        previous = df[column].shift(1)

        if compare_to is not None:
            if isinstance(compare_to, str):
                compare_to = df[compare_to]

            crossing_above = (current > compare_to) & (previous <= compare_to)
            crossing_below = (current < compare_to) & (previous >= compare_to)

        else:
            crossing_above = (current > 0) & (previous <= 0)
            crossing_below = (current < 0) & (previous >= 0)

        df.loc[crossing_above, signal_name] = 1
        df.loc[crossing_below, signal_name] = -1

        return df  # This line should be outside the if-else block

    # Apply Crossover Signals
    df = crossover_signal(df, 'MACD_12_26_9', 'signal_MACD_line', 'MACDs_12_26_9')
    df = crossover_signal(df, 'MACD_12_26_9', 'signal_MACD_zero', None)

    

    return df

def process_signal_AO(df):

    df['AO'] = ao(df['High'], df['Low'], fast=5, slow=34, kind='price')
    df['AO_shift1'] = df['AO'].shift(1)

    # Assuming df['AO'] is already calculated
    df['signal_AO'] = 0

    # Shift AO to get previous value
    df['AO_shift1'] = df['AO'].shift(1)

    # Zero Line Cross
    df.loc[(df['AO'] > 0) & (df['AO_shift1'] <= 0), 'signal_AO'] = 1  # Bullish crossover
    df.loc[(df['AO'] < 0) & (df['AO_shift1'] >= 0), 'signal_AO'] = -1 # Bearish crossover

    # Directional Change (optional)
    df.loc[(df['AO'] > df['AO_shift1']) & (df['AO'] > 0), 'signal_AO'] = 1   # Strengthening Bullish Momentum
    df.loc[(df['AO'] < df['AO_shift1']) & (df['AO'] < 0), 'signal_AO'] = -1  # Strengthening Bearish Momentum

    

    return df

def process_signal_aroon(df, length=14):
    # Calculate Aroon indicator
    aroon_result = aroon(df["High"], df["Low"], length=length)
    df = df.join(aroon_result)

    # Initialize Aroon signal column
    df['signal_Aroon'] = 0

    # Crossover Signals
    df.loc[df['AROONU_14'] > df['AROOND_14'], 'signal_Aroon'] = 1   # Bullish crossover
    df.loc[df['AROONU_14'] < df['AROOND_14'], 'signal_Aroon'] = -1  # Bearish crossover

    # Optional: Trend Strength (e.g., Aroon Up or Down above 70)
    df.loc[df['AROONU_14'] > 70, 'signal_Aroon'] = 1  # Strong bullish trend
    df.loc[df['AROOND_14'] > 70, 'signal_Aroon'] = -1 # Strong bearish trend

    

    return df

def process_signal_bbstop(df, n=20, k=2):

    # Calculate the moving average
    df['MA'] = df['Close'].rolling(window=n).mean()

    df['STD'] = df['Close'].rolling(window=n).std()

    # Calculate the upper and lower Bollinger Bands
    df['Upper_BB'] = df['MA'] + k * df['STD']
    df['Lower_BB'] = df['MA'] - k * df['STD']

    # Calculate the BBStop
    df['BBStop'] = (df['Upper_BB'] + df['Lower_BB']) / 2

    # Determine the signal_bbstop
    # 1 for uptrend (close above upper band), -1 for downtrend (close below lower band)
    df['signal_bbstop'] = 0
    df.loc[df['Close'] > df['BBStop'], 'signal_bbstop'] = 1
    df.loc[df['Close'] < df['BBStop'], 'signal_bbstop'] = -1

    return df

def process_signal_wpr(df, period=14):
    # Calculate Williams %R
    df['WPR'] = willr(df['High'], df['Low'], df['Close'], length=period)
    
    # Initialize signal column
    df['signal_wpr'] = 0

    # Generate signals
    # Buy signal: WPR crosses above -80 from below
    bullish = (df['WPR'].shift(1) < -80) & (df['WPR'] > -80)
    df.loc[bullish, 'signal_wpr'] = 1

    # Sell signal: WPR crosses below -20 from above
    bearish = (df['WPR'].shift(1) > -20) & (df['WPR'] < -20)
    df.loc[bearish, 'signal_wpr'] = -1

    return df

def process_signal_GMMA(df, ma_lengths=[3, 5, 8, 10, 12,15, 30, 35, 40, 45, 50, 60]):
    # Calculate GMMA EMAs for each specified length
    for length in ma_lengths:
        df[f"GMMA_{length}"] = ta.ema(df["Close"], length=length)

    # Initialize signal columns for each MA comparison
    for i, short_len in enumerate(ma_lengths):
        for long_len in ma_lengths[i+1:]:
            signal_col = f"signal_GMMA_{short_len}_{long_len}"
            df[signal_col] = 0
            # Generate signals for each MA pair
            df.loc[df[f"GMMA_{short_len}"] > df[f"GMMA_{long_len}"], signal_col] = 1   # Bullish
            df.loc[df[f"GMMA_{short_len}"] < df[f"GMMA_{long_len}"], signal_col] = -1  # Bearish

    return df

def calculate_signals(input_df, timeframe):
    
    raw_df = input_df.copy()

    df = process_raw_data(raw_df, timeframe)

    df = process_singal_ichimoku(df)
    df = process_signal_macd(df)
    df = process_signal_AO(df)
    df = process_signal_aroon(df)
    df = process_signal_bbstop(df)
    df = process_signal_wpr(df)
    df = process_signal_GMMA(df)

    # Keep only columns that include "signal"
    signal_columns = [col for col in df.columns if 'signal' in col]
    df = df[signal_columns]

    # Append the timeframe as a suffix to all signal columns
    df.rename(columns={col: f"{col}_{timeframe}" for col in df.columns}, inplace=True)

    return df

def process_raw_data(raw_df, timeframe):
    df = raw_df.copy()
    
    if not isinstance(df.index, pd.DatetimeIndex):
        df['Time'] = pd.to_datetime(df['Time'], format='%Y-%m-%d %H:%M:%S', errors='coerce')
        df = df.set_index('Time')
    
    resampling_rules = {
        '1min': '1T',
        '5min': '5T',
        '15min': '15T',
        '1hour': '1H',
        '4hour': '4H',
        '1day': '1D'
    }

    rule = resampling_rules.get(timeframe)
    
    if rule:
        df_resampled = df.resample(rule).agg({'Open': 'first',
                                              'High': 'max',
                                              'Low': 'min',
                                              'Close': 'last'})

        df_resampled.dropna(inplace=True)
        return df_resampled
    else:
        raise ValueError("Invalid timeframe. Choose from '1min', '5min', '15min', '1hour', '4hour',or '1day'.")

class MarginTradeSimulator:
    def __init__(self, price_series, leverage=LEVERAGE, initial_balance=INITIAL_BALANCE, stop_out_level=STOP_OUT, risk_free_rate=RISK_FREE_RATE, sim_debug=False):
        # Price series
        self.price_series = price_series
        # self.spot_prcie = 0
        self.timestep = 0
        self.timecount = 0
                
        # Account balance and equity
        self.pattern = self.generate_balance_pattern()
        # self.initial_balance = self.get_random_balance()
        self.initial_balance = initial_balance
        self.balance = initial_balance
        self.equity = initial_balance
        
        # Profit and Loss
        self.unrealized_pnl = 0
        self.realized_pnl = 0
        
        # Positions
        self.nop = 0
        self.avg_price = 0
        self.spot_price = 0
        
        # Leverage and margin
        self.leverage = leverage
        self.margin_required = 0
        self.margin_free = self.equity - self.margin_required
        self.margin_level = 100
        self.stop_out_level = stop_out_level
        self.portfolio_leverage = abs(self.nop) / self.equity if self.equity != 0 else 0
        
        # Performance metrics
        self.risk_free_rate = risk_free_rate
        self.equity_history = [initial_balance]
        self.max_drawdown = 0
        self.sortino_ratio = 0
        self.return_percentage = 0
        self.log_return = 0
        self.accumulated_cost = 0
        
        # Trading details
        self.trade_not_executed = False
        self.invalid_action = False
        self.stopped_out = False
        self.goal_reached = False
        self.sim_debug = sim_debug
        self.trade_cost = 0
        self.trade_profit = 0
        
        # reward parameters
        self.reward = 0
        
        self.realized_pnl_history = []
        
        self.timestamp = None # Initialize timestamp
        
        self.episode_start_index = 0

        self.reset()
            
    def reset(self):
        # Price series
        self.timestep = 0
        self.timecount = 0
        self.episode_start_index = 0
        self.timestamp = None # Initialize timestamp
        
        self.spot_price = 0

        # Account balance and equity
        self.initial_balance = self.initial_balance
        self.balance = self.initial_balance
        self.equity_history = []
        self.reward = 0

        # Profit and Loss
        self.realized_pnl = 0
        self.unrealized_pnl = 0

        # Positions
        self.nop = 0
        self.avg_price = 0
        self.done_trades = []

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
               
        self.account_info = {
            "timestep": self.timestep,
            "equity": self.equity,
            "balance": self.balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
            "reward": self.reward,
        }
        
        self.previous_account_info = {
            "timestep": 0,
            "equity": self.initial_balance,
            "balance": self.initial_balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
        }

        self.open_positions = {
            "nop": self.nop,
            "avg_price": self.avg_price,
            "spot_price": self.spot_price,
            "Floating_Profit": self.unrealized_pnl,
            "trade_not_executed": 1 if self.trade_not_executed else 0,
            "stopped_out": 1 if self.stopped_out else 0
        }
        
        self.done_trades = []

        self.update_account_info_and_open_positions()
        
        self.record = []
        
        # self.print_summary()

    def generate_balance_pattern(self):
        pattern = [10_000]
        while pattern[-1] < 1_000_000:
            # Multiply the last value by 5 if it's divisible by 10_000, else by 2
            next_value = pattern[-1] * 5 if pattern[-1] % (5 * 10_000) == 0 else pattern[-1] * 2
            pattern.append(next_value)
        return pattern

    def get_random_balance(self):
        # Select a random value from the pattern
        return random.choice(self.pattern)

    def is_end_of_day(self):
        return self.timecount % MAX_TIMESTEP == 0

    def execute(self, action, price, volume):
        # self.timecount += 1
        
        self.spot_price = price
                
        self.trade_not_executed = False  # Reset the trade_not_executed attribute
        self.previous_account_info = self.account_info.copy()
        
        margin_impact = self.calculate_margin_impact(action, price, volume)
        margin_level_after_trade = self.account_info["margin_level"] + margin_impact["margin_level_change"]
        margin_free_after_trade = self.account_info["margin_free"] - margin_impact["margin_required_change"]

        if margin_level_after_trade >= self.stop_out_level and margin_free_after_trade > 0:
            # Execute the trade action (Hold/Long/Short)
            if action == 0:  # Hold
                self.hold(price)
            elif action == 1:  # Long
                self.long(price, volume)
            elif action == 2:  # Short
                self.short(price, volume)
        else:
            if self.sim_debug:
                print("Trade not executed. Insufficient free margin or margin level would fall below stop-out level.")
            self.trade_not_executed = True
            self.hold(price)    # hold method will update acc_info with price
                           
        self.reward = self.calculate_reward()
        self.account_info["reward"] = self.reward
            
        self.create_record()       
        
    def check_goal(self, price):
        self.spot_price = price
        self.unrealized_pnl = self.calculate_unrealized_pnl()
        self.update_account_info_and_open_positions()
        
        # Check if goal reached
        if self.account_info["return_percentage"] > MAX_PROFIT_TARGET:
            self.goal_reached = True
            self.close(price)
            
        if self.sim_debug:
            print("Goal reached. Closing all positions.")
            
    def check_stopped_out(self, price):
        self.spot_price = price
        self.unrealized_pnl = self.calculate_unrealized_pnl()
        self.update_account_info_and_open_positions()
        
        # Check if stop out occurred
        # if self.account_info["margin_level"] < self.stop_out_level:
        if self.return_percentage < CUT_LOSS:
            self.stopped_out = True
            self.close(price)
            
        if self.sim_debug:
            print("Stop-out event. Closing all positions.")
            
    def calculate_margin_impact(self, action, price, volume):
        # Calculate the margin required for the potential trade
        margin_required_trade = self.calculate_margin_required(price, volume)

        # Estimate the new margin required after the trade
        margin_required_new = self.account_info["margin_required"]
        if action == 1 or action == 2:  # Long or Short
            margin_required_new += margin_required_trade
        elif action == 3:  # Close
            margin_required_new = 0

        # Estimate the new equity after the trade
        equity_new = self.equity
        if action == 3:  # Close
            equity_new += self.unrealized_pnl

        # Calculate the new margin free and margin level
        margin_free_new = equity_new - margin_required_new
        margin_level_new = (equity_new / margin_required_new) * 100 if margin_required_new != 0 else float("inf")

        return {
            "margin_required_change": margin_required_new - self.account_info["margin_required"],
            "margin_free_change": margin_free_new - self.account_info["margin_free"],
            "margin_level_change": margin_level_new - self.account_info["margin_level"]
        }
        
    def generate_trading_cost(self, price, volume):
        trading_cost_percentage = TRADING_FEE
        trading_cost = trading_cost_percentage * abs(volume)
        return trading_cost

    def hold(self, price):
        self.spot_price = price
        self.unrealized_pnl = self.calculate_unrealized_pnl()
        self.update_account_info_and_open_positions()
        
        if self.sim_debug:
            print(f"Hold and update the price {price: ,.5f}")
        
        return {
        "done_trades": self.done_trades,
        "open_positions": self.open_positions,
        "account_info": self.account_info,
    }
    
    def long(self, price, vol):
        realized_pnl = 0

        # Case 1: when nop = 0
        if self.nop == 0:
            self.trade_profit = 0
            
            self.trade_cost = self.generate_trading_cost(price, vol)
            
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop = vol
            self.avg_price = price
            self.spot_price = price
            self.unrealized_pnl = 0

        # Case 2: when nop > 0
        elif self.nop > 0:
            self.trade_profit = 0
            self.trade_cost = self.generate_trading_cost(price, vol)
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop += vol
            self.avg_price = (self.avg_price * (self.nop - vol) + price * vol) / self.nop
            self.spot_price = price
            self.unrealized_pnl = (price - self.avg_price) * self.nop

        # Case 3: when nop < 0 (position changes from short to long)
        else:
            # Case 3a: vol > abs(nop)
            if vol > abs(self.nop):
                remaining_vol = vol - abs(self.nop)

                # Close short position
                realized_pnl = (self.avg_price - price) * (abs(self.nop) / price)
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades for closing short position
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, abs(self.nop))
                self.accumulated_cost += self.trade_cost
                self.create_trade("buy", price, abs(self.nop), self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions for closing short position
                self.nop = remaining_vol
                self.avg_price = price

            # Case 3b: vol < abs(nop)
            elif vol < abs(self.nop):
                # Calculate realized P&L
                realized_pnl = (self.avg_price - price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop += vol  # Reduce the magnitude of the short position
                # Do not change the average price as it's still the same short position

            # Case 3c: vol = abs(nop)
            else:
                # Calculate realized P&L
                realized_pnl = (self.avg_price - price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("buy", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop = 0
                self.avg_price = 0

        # Update open_positions
        self.spot_price = price
        self.unrealized_pnl = (price - self.avg_price) * self.nop

        self.update_account_info_and_open_positions()
        
        if self.sim_debug:
            print(f"Long at {price: ,.5f} for {vol: ,.2f} with trading cost {self.trade_cost: ,.2f}")

        return {
                "done_trades": self.done_trades,
                "open_positions": self.open_positions,
                "account_info": self.account_info,
            }

    def short(self, price, vol):
        realized_pnl = 0

        # Case 1: when nop = 0
        if self.nop == 0:
            self.trade_profit = 0
            self.trade_cost = self.generate_trading_cost(price, vol)
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop = -vol
            self.avg_price = price
            self.spot_price = price
            self.unrealized_pnl = 0

        # Case 2: when nop < 0
        elif self.nop < 0:
            self.trade_profit = 0
            self.trade_cost = self.generate_trading_cost(price, vol)
            self.accumulated_cost += self.trade_cost

            # Update done_trades
            self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

            # Update balance
            self.balance -= self.trade_cost

            # Update open_positions
            self.nop -= vol
            self.avg_price = ((abs(self.nop) + vol) * self.avg_price - price * vol) / abs(self.nop)
            self.spot_price = price
            self.unrealized_pnl = (self.avg_price - price) * abs(self.nop)

        # Case 3: when nop > 0 (position changes from long to short)
        else:
            # Case 3a: vol > nop
            if vol > self.nop:
                remaining_vol = vol - self.nop

                # Close long position
                realized_pnl = (price - self.avg_price) * abs(self.nop) / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades for closing long position
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, self.nop)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, self.nop, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions for closing long position
                self.nop = 0
                self.avg_price = 0

                # Open new short position with remaining_vol
                self.nop = -remaining_vol
                self.avg_price = price

                # Update done_trades for opening new short position
                self.trade_profit = 0
                self.trade_cost = self.generate_trading_cost(price, remaining_vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, remaining_vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

            # Case 3b: vol < nop
            elif vol < self.nop:
                # Calculate realized P&L
                realized_pnl = (price - self.avg_price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop -= vol  # Reduce the magnitude of the long position
                # Do not change the average price as it's still the same long position

            # Case 3c: vol = nop
            else:
                # Calculate realized P&L
                realized_pnl = (price - self.avg_price) * vol / price
                self.realized_pnl += realized_pnl
                self.balance += realized_pnl

                # Update done_trades
                self.trade_profit = realized_pnl
                self.trade_cost = self.generate_trading_cost(price, vol)
                self.accumulated_cost += self.trade_cost
                self.create_trade("sell", price, vol, self.trade_profit, self.trade_cost)

                # Update balance
                self.balance -= self.trade_cost

                # Update open_positions
                self.nop = 0
                self.avg_price = 0

        # Update open_positions
        self.spot_price = price
        self.unrealized_pnl = (price - self.avg_price) * self.nop
        

        self.update_account_info_and_open_positions()
        
        if self.sim_debug:
            print(f"Short at {price: ,.5f} for {vol: ,.2f} with trading cost {self.trade_cost: ,.2f}")

        return {
                "done_trades": self.done_trades,
                "open_positions": self.open_positions,
                "account_info": self.account_info,
            }

    def close(self, price):
        if self.nop == 0:
            return
        
        if self.nop > 0:
            self.short(price, abs(self.nop))
        elif self.nop < 0:
            self.long(price, abs(self.nop))
        
        # Update the reward after closing the positions
        self.reward = self.calculate_reward()
        self.account_info["reward"] = self.reward
        
        # Create a record of the current state after closing the positions
        self.create_record()

    def create_trade(self, side, price, quantity, trade_profit, trade_cost):
        
        trade = {
            "time": self.timestep,
            "symbol": "contract",
            "side": side,
            "price": price,
            "quantity": quantity,
            "trade_profit": trade_profit,
            "trade_cost": self.trade_cost,
        }
        self.done_trades.append(trade)
        
    def create_record(self):
        
        record_entry = {
            "timecount": self.timecount,
            "timestamp": self.timestamp,
            "timestep": self.timestep,
            "equity": self.equity,
            "balance": self.balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
            "reward": self.reward,
            "avg_price": self.avg_price,
            "spot_price": self.spot_price,
            "Floating_Profit": self.unrealized_pnl,
            "trade_not_executed": 1 if self.trade_not_executed else 0,
            "stopped_out": 1 if self.stopped_out else 0
        }
        self.record.append(record_entry)

    def update_timestamp(self, timestamp):
        self.timestamp = timestamp

    def check_margin_level(self):
        if self.equity == 0 or self.margin_required == 0:
            self.margin_level = 100
        else:
            self.margin_level = (self.equity / self.margin_required) * 100

    def update_account_info_and_open_positions(self):
       
        self.equity = self.balance + self.unrealized_pnl
        self.margin_required = self.calculate_margin_required(self.spot_price, abs(self.nop))
        self.margin_free = self.equity - self.margin_required

        self.check_margin_level()  # Add this line to update margin_level

        self.portfolio_leverage = abs(self.nop) / self.equity if self.equity != 0 else 0

        # Calculate return and log return
        self.return_percentage = (self.equity - self.initial_balance) / self.initial_balance * 100
        if self.equity > 0 and self.initial_balance > 0:
            self.log_return = np.log(self.equity / self.initial_balance)
        else:
            self.log_return = 0  # Or any other value that makes sense in this context

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
        running_max_equity = np.maximum.accumulate(self.equity_history)
        drawdowns = (running_max_equity - self.equity_history) / running_max_equity
        self.max_drawdown = np.max(drawdowns) * 100 if len(drawdowns) > 0 else 0
        
        self.avg_price = self.calculate_avg_price()
                       
        self.account_info = {
            "timestep": self.timestep,
            "equity": self.equity,
            "balance": self.balance,
            "nop": self.nop,
            "realized_pnl": self.realized_pnl,
            "unrealized_pnl": self.unrealized_pnl,
            "accumulated_cost": self.accumulated_cost,
            "margin_required": self.margin_required,
            "margin_free": self.margin_free,
            "margin_level": self.margin_level,
            "portfolio_leverage": self.portfolio_leverage,
            "return_percentage": self.return_percentage,
            "log_return": self.log_return,
            "max_drawdown": self.max_drawdown,
            "sortino_ratio": self.sortino_ratio,
            "reward": self.reward,
        }
        
        self.open_positions = {
            "nop": self.nop,
            "avg_price": self.avg_price,
            "spot_price": self.spot_price,
            "Floating_Profit": self.unrealized_pnl,
            "trade_not_executed": 1 if self.trade_not_executed else 0,
            "stopped_out": 1 if self.stopped_out else 0
        }

        self.equity_history.append(self.equity)
            
    def calculate_nop(self):
        """
        Calculates the net open position (NOP) from the list of done trades.
        """
        total_quantity = 0

        for trade in self.done_trades:
            if trade["side"] == "buy":
                total_quantity += trade["quantity"]
            else:
                total_quantity -= trade["quantity"]

        return total_quantity
        
    def calculate_avg_price(self):
        """
        Calculates the average price of all open positions.
        """
        total_notional = 0
        total_quantity = 0

        for trade in self.done_trades:
            if trade["side"] == "buy":
                total_notional += trade["price"] * trade["quantity"]
                total_quantity += trade["quantity"]
            else:
                total_notional -= trade["price"] * trade["quantity"]
                total_quantity -= trade["quantity"]

        if total_quantity == 0:
            return 0

        return total_notional / total_quantity

    def calculate_unrealized_pnl(self):
        """
        Calculates the unrealized P&L for all open positions.
        """
        if self.nop > 0:
            # Long position
            return (self.spot_price - self.avg_price) * (abs(self.nop) / self.spot_price)
        elif self.nop < 0:
            # Short position
            return (self.avg_price - self.spot_price) * (abs(self.nop) / self.spot_price)
        else:
            # No open positions
            return 0

    def calculate_margin_required(self, price, volume):
        margin_required = (abs(volume)) / self.leverage
        return margin_required

    def calculate_portfolio_leverage(self):
        """
        Calculate the portfolio leverage.
        Leverage = (Total Exposure) / Equity
        Total Exposure = NOP * Spot Price
        """
        if self.equity == 0:
            return 0

        total_exposure = abs(self.nop) * self.spot_price
        leverage = total_exposure / self.equity
        return leverage

    def set_reward_coefficients(self, **kwargs):
        for key, value in kwargs.items():
            if key in self.reward_coefficients:
                self.reward_coefficients[key] = value
            else:
                raise ValueError(f"Invalid reward coefficient: {key}")

    def plot_equity_history(self):
        plt.figure(figsize=(12, 6))
        plt.plot(self.equity_history)
        plt.xlabel("Timestep")
        plt.ylabel("Equity")
        plt.title("Equity History")
        plt.grid(True)
        plt.show()

    def summarize_done_trades(self):
        num_trades = len(self.done_trades)
        total_profit = sum(trade['trade_profit'] for trade in self.done_trades)
        average_profit = total_profit / num_trades if num_trades > 0 else 0

        winning_trades = [trade for trade in self.done_trades if trade['trade_profit'] > 0]
        losing_trades = [trade for trade in self.done_trades if trade['trade_profit'] <= 0]

        num_winning_trades = len(winning_trades)
        num_losing_trades = len(losing_trades)
        win_rate = num_winning_trades / num_trades if num_trades > 0 else 0

        summary = {
            'num_trades': num_trades,
            'total_profit': total_profit,
            'average_profit': average_profit,
            'win_rate': win_rate,
            'num_winning_trades': num_winning_trades,
            'num_losing_trades': num_losing_trades,
        }

        return summary

    def print_summary(self):
        print("\nAccount Information:")
        for key, value in self.account_info.items():
            if key in ['avg_price', 'spot_price']:
                print(f"{key.capitalize():<16}: {value:,.5f}")
            else:
                print(f"{key.capitalize():<16}: {value:,.2f}")
                
            if key in ['reward']:
                print(f"{key.capitalize():<16}: {value:,.6f}")
                
        print("\nPrevious Account Information:")
                
        for key, value in self.previous_account_info.items():
            if key in ['avg_price', 'spot_price']:
                print(f"{key.capitalize():<16}: {value:,.5f}")
            else:
                print(f"{key.capitalize():<16}: {value:,.2f}")

        print("\nOpen Positions:")
        for key, value in self.open_positions.items():
            if isinstance(value, float):
                if key in ['avg_price', 'spot_price']:
                    print(f"{key.capitalize():<16}: {value:,.5f}")
                else:
                    print(f"{key.capitalize():<16}: {value:,.2f}")
            else:
                print(f"{key.capitalize():<16}: {value}")

        print("\nDone Trades:")
        for trade in self.done_trades:
            trade_str = ", ".join(
                [f"{key.capitalize()}: {value:,.5f}" if key in ['price'] else f"{key.capitalize()}: {value:,.2f}" if isinstance(
                    value, (int, float)) else f"{key.capitalize()}: {value}" for key, value in trade.items()])
            print(f"{{{trade_str}}}")           

    def calculate_reward(self, weight_dense=0.8, weight_sparse=1, weight_shaping=0.3,
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
